#include <thread>
#include <atomic>
#include <chrono>
#include <cinttypes>

#include "ultra64.h"
#include "multilibultra.hpp"
#include "recomp.h"

static struct {
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
        int retrace_count = 1;
    } vi;
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
        OSTask task;
        std::atomic_flag task_queued;
    } sp;
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } dp;
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } ai;
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
        std::atomic_flag read_queued;
    } si;
    // The same message queue may be used for multiple events, so share a mutex for all of them
    std::mutex message_mutex;
    uint8_t* rdram;
    std::chrono::system_clock::time_point start;
} events_context{};

extern "C" void osSetEventMesg(RDRAM_ARG OSEvent event_id, PTR(OSMesgQueue) mq_, OSMesg msg) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    std::lock_guard lock{ events_context.message_mutex };

    switch (event_id) {
        case OS_EVENT_SP:
            events_context.sp.msg = msg;
            events_context.sp.mq = mq_;
            break;
        case OS_EVENT_DP:
            events_context.dp.msg = msg;
            events_context.dp.mq = mq_;
            break;
        case OS_EVENT_AI:
            events_context.ai.msg = msg;
            events_context.ai.mq = mq_;
            break;
        case OS_EVENT_SI:
            events_context.si.msg = msg;
            events_context.si.mq = mq_;
    }
}

extern "C" void osViSetEvent(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, u32 retrace_count) {
    std::lock_guard lock{ events_context.message_mutex };
    events_context.vi.mq = mq_;
    events_context.vi.msg = msg;
    events_context.vi.retrace_count = retrace_count;
}

// N64 CPU counter ticks per millisecond
constexpr uint32_t counter_per_ms = 46'875;

uint64_t duration_to_count(std::chrono::system_clock::duration duration) {
    uint64_t delta_micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    // More accurate than using a floating point timer, will only overflow after running for 12.47 years
    // Units: (micros * (counts/millis)) / (micros/millis) = counts
    uint64_t total_count = (delta_micros * counter_per_ms) / 1000;

    return total_count;
}

extern "C" u32 osGetCount() {
    uint64_t total_count = duration_to_count(std::chrono::system_clock::now() - events_context.start);

    // Allow for overflows, which is how osGetCount behaves
    return (uint32_t)total_count;
}

extern "C" OSTime osGetTime() {
    uint64_t total_count = duration_to_count(std::chrono::system_clock::now() - events_context.start);

    return total_count;
}

void vi_thread_func() {
    using namespace std::chrono_literals;
    
    events_context.start = std::chrono::system_clock::now();
    uint64_t total_vis = 0;
    int remaining_retraces = events_context.vi.retrace_count;

    while (true) {
        // Determine the next VI time (more accurate than adding 16ms each VI interrupt)
        auto next = events_context.start + (total_vis * 1000000us) / 60;
        //if (next > std::chrono::system_clock::now()) {
        //    printf("Sleeping for %" PRIu64 " us to get from %" PRIu64 " us to %" PRIu64 " us \n",
        //        (next - std::chrono::system_clock::now()) / 1us,
        //        (std::chrono::system_clock::now() - events_context.start) / 1us,
        //        (next - events_context.start) / 1us);
        //} else {
        //    printf("No need to sleep\n");
        //}
        std::this_thread::sleep_until(next);
        // Calculate how many VIs have passed
        uint64_t new_total_vis = ((std::chrono::system_clock::now() - events_context.start) * 60 / 1000ms) + 1;
        if (new_total_vis > total_vis + 1) {
            printf("Skipped % " PRId64 " frames in VI interupt thread!\n", new_total_vis - total_vis - 1);
        }
        total_vis = new_total_vis;

        remaining_retraces--;

        if (remaining_retraces == 0) {
            std::lock_guard lock{ events_context.message_mutex };
            remaining_retraces = events_context.vi.retrace_count;
            uint8_t* rdram = events_context.rdram;

            if (events_context.vi.mq != NULLPTR) {
                if (osSendMesg(PASS_RDRAM events_context.vi.mq, events_context.vi.msg, OS_MESG_NOBLOCK) == -1) {
                    //printf("Game skipped a VI frame!\n");
                }
            }
        }
    }
}

void sp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    osSendMesg(PASS_RDRAM events_context.sp.mq, events_context.sp.msg, OS_MESG_NOBLOCK);
}

void dp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    osSendMesg(PASS_RDRAM events_context.dp.mq, events_context.dp.msg, OS_MESG_NOBLOCK);
}

void gfx_thread_func() {
    while (true) {
        // Wait for a sp task to be queued
        events_context.sp.task_queued.wait(false);

        // Grab the task and inform the game that it's free to queue up a new task
        OSTask current_task = events_context.sp.task;
        events_context.sp.task_queued.clear();
        events_context.sp.task_queued.notify_all();

        // Process the task
        if (current_task.t.type = M_GFXTASK) {
            // TODO interface with RT64 here
            
            // (TODO let RT64 do this) Tell the game that the RSP and RDP tasks are complete
            sp_complete();
            dp_complete();
        } else if (current_task.t.type == M_AUDTASK) {
            sp_complete();
        } else {
            fprintf(stderr, "Unknown task type: %" PRIu32 "\n", current_task.t.type);
            std::exit(EXIT_FAILURE);
        }
    }
}

void Multilibultra::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {
    OSTask* task = TO_PTR(OSTask, task_);
    // Wait for the sp thread clear the old task
    events_context.sp.task_queued.wait(true);
    // Make a full copy of the task instead of just recording a pointer to it, since that's what osSpTaskLoad does
    events_context.sp.task = *task;

    events_context.sp.task_queued.test_and_set();
    events_context.sp.task_queued.notify_all();
}

void Multilibultra::send_si_message() {
    uint8_t* rdram = events_context.rdram;
    osSendMesg(PASS_RDRAM events_context.si.mq, events_context.si.msg, OS_MESG_NOBLOCK);
}

void Multilibultra::init_events(uint8_t* rdram) {
    events_context.rdram = rdram;
    events_context.vi.thread = std::thread{ vi_thread_func };
    events_context.sp.thread = std::thread{ gfx_thread_func };
}