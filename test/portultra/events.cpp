#include <thread>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <variant>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <queue>

#include <Windows.h>
#include "SDL.h"
#include "blockingconcurrentqueue.h"

#include "ultra64.h"
#include "multilibultra.hpp"
#include "recomp.h"
#include "../src/rsp.h"

struct SpTaskAction {
    OSTask task;
};

struct SwapBuffersAction {
    uint32_t origin;
};

using Action = std::variant<SpTaskAction, SwapBuffersAction>;

static struct {
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        PTR(void) current_buffer = NULLPTR;
        PTR(void) next_buffer = NULLPTR;
        OSMesg msg = (OSMesg)0;
        int retrace_count = 1;
    } vi;
    struct {
        std::thread thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
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
    } si;
    // The same message queue may be used for multiple events, so share a mutex for all of them
    std::mutex message_mutex;
    uint8_t* rdram;
    moodycamel::BlockingConcurrentQueue<Action> action_queue{};
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

void vi_thread_func() {
    using namespace std::chrono_literals;
    
    uint64_t total_vis = 0;
    int remaining_retraces = events_context.vi.retrace_count;

    while (true) {
        // Determine the next VI time (more accurate than adding 16ms each VI interrupt)
        auto next = Multilibultra::get_start() + (total_vis * 1000000us) / (60 * Multilibultra::get_speed_multiplier());
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
        uint64_t new_total_vis = (Multilibultra::time_since_start() * (60 * Multilibultra::get_speed_multiplier()) / 1000ms) + 1;
        if (new_total_vis > total_vis + 1) {
            //printf("Skipped % " PRId64 " frames in VI interupt thread!\n", new_total_vis - total_vis - 1);
        }
        total_vis = new_total_vis;

        remaining_retraces--;

        {
            std::lock_guard lock{ events_context.message_mutex };
            uint8_t* rdram = events_context.rdram;
            if (remaining_retraces == 0) {
                remaining_retraces = events_context.vi.retrace_count;

                if (events_context.vi.mq != NULLPTR) {
                    if (osSendMesg(PASS_RDRAM events_context.vi.mq, events_context.vi.msg, OS_MESG_NOBLOCK) == -1) {
                        //printf("Game skipped a VI frame!\n");
                    }
                }
            }
            if (events_context.ai.mq != NULLPTR) {
                if (osSendMesg(PASS_RDRAM events_context.ai.mq, events_context.ai.msg, OS_MESG_NOBLOCK) == -1) {
                    //printf("Game skipped a AI frame!\n");
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

void RT64Init(uint8_t* rom, uint8_t* rdram);
void RT64SendDL(uint8_t* rdram, const OSTask* task);
void RT64UpdateScreen(uint32_t vi_origin);

std::unordered_map<SDL_Scancode, int> button_map{
    { SDL_Scancode::SDL_SCANCODE_LEFT,   0x0002 }, // c left
    { SDL_Scancode::SDL_SCANCODE_RIGHT,  0x0001 }, // c right
    { SDL_Scancode::SDL_SCANCODE_UP,     0x0008 }, // c up
    { SDL_Scancode::SDL_SCANCODE_DOWN,   0x0004 }, // c down
    { SDL_Scancode::SDL_SCANCODE_RETURN, 0x1000 }, // start
    { SDL_Scancode::SDL_SCANCODE_SPACE,  0x8000 }, // a
    { SDL_Scancode::SDL_SCANCODE_LSHIFT, 0x4000 }, // b
    { SDL_Scancode::SDL_SCANCODE_Q,      0x2000 }, // z
    { SDL_Scancode::SDL_SCANCODE_E,      0x0020 }, // l
    { SDL_Scancode::SDL_SCANCODE_R,      0x0010 }, // r
    { SDL_Scancode::SDL_SCANCODE_J,      0x0200 }, // dpad left
    { SDL_Scancode::SDL_SCANCODE_L,      0x0100 }, // dpad right
    { SDL_Scancode::SDL_SCANCODE_I,      0x0800 }, // dpad up
    { SDL_Scancode::SDL_SCANCODE_K,      0x0400 }, // dpad down
};

extern int button;
extern int stick_x;
extern int stick_y;

int sdl_event_filter(void* userdata, SDL_Event* event) {
    switch (event->type) {
    case SDL_EventType::SDL_KEYUP:
    case SDL_EventType::SDL_KEYDOWN:
        {
            const Uint8* key_states = SDL_GetKeyboardState(nullptr);
            int new_button = 0;

            for (const auto& mapping : button_map) {
                if (key_states[mapping.first]) {
                    new_button |= mapping.second;
                }
            }

            button = new_button;

            stick_x = 127 * (key_states[SDL_Scancode::SDL_SCANCODE_D] - key_states[SDL_Scancode::SDL_SCANCODE_A]);
            stick_y = 127 * (key_states[SDL_Scancode::SDL_SCANCODE_W] - key_states[SDL_Scancode::SDL_SCANCODE_S]);
        }
        break;
    case SDL_EventType::SDL_QUIT:
        std::quick_exit(ERROR_SUCCESS);
        break;
    }
    return 1;
}

uint8_t dmem[0x1000];
uint16_t rspReciprocals[512];
uint16_t rspInverseSquareRoots[512];

using RspUcodeFunc = RspExitReason(uint8_t* rdram);
extern RspUcodeFunc njpgdspMain;
extern RspUcodeFunc aspMain;

// From Ares emulator. For license details, see rsp_vu.h
void rsp_constants_init() {
    rspReciprocals[0] = u16(~0);
    for (u16 index = 1; index < 512; index++) {
        u64 a = index + 512;
        u64 b = (u64(1) << 34) / a;
        rspReciprocals[index] = u16(b + 1 >> 8);
    }

    for (u16 index = 0; index < 512; index++) {
        u64 a = index + 512 >> ((index % 2 == 1) ? 1 : 0);
        u64 b = 1 << 17;
        //find the largest b where b < 1.0 / sqrt(a)
        while (a * (b + 1) * (b + 1) < (u64(1) << 44)) b++;
        rspInverseSquareRoots[index] = u16(b >> 1);
    }
}

// Runs a recompiled RSP microcode
void run_rsp_microcode(uint8_t* rdram, const OSTask* task, RspUcodeFunc* ucode_func) {
    // Load the OSTask into DMEM
    memcpy(&dmem[0xFC0], task, sizeof(OSTask));
    // Load the ucode data into DMEM
    dma_rdram_to_dmem(rdram, 0x0000, task->t.ucode_data, 0xF80 - 1);
    // Run the ucode
    RspExitReason exit_reason = ucode_func(rdram);
    // Ensure that the ucode exited correctly
    assert(exit_reason == RspExitReason::Broke);
    sp_complete();
}

void event_thread_func(uint8_t* rdram, uint8_t* rom, std::atomic_flag* events_thread_ready) {
    using namespace std::chrono_literals;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
        std::quick_exit(EXIT_FAILURE);
    }
    RT64Init(rom, rdram);
    SDL_Window* window = SDL_GetWindowFromID(1);
    // TODO set this window title in RT64, create the window here and send it to RT64, or something else entirely
    // as the current window name visibly changes as RT64 is initialized
    SDL_SetWindowTitle(window, "Recomp");
    //SDL_SetEventFilter(sdl_event_filter, nullptr);

    rsp_constants_init();

    // Notify the caller thread that this thread is ready.
    events_thread_ready->test_and_set();
    events_thread_ready->notify_all();

    while (true) {
        // Try to pull an action from the queue
        Action action;
        if (events_context.action_queue.wait_dequeue_timed(action, 1ms)) {
            // Determine the action type and act on it
            if (const auto* task_action = std::get_if<SpTaskAction>(&action)) {
                if (task_action->task.t.type == M_GFXTASK) {
                    // (TODO let RT64 do this) Tell the game that the RSP and RDP tasks are complete
                    RT64SendDL(rdram, &task_action->task);
                    sp_complete();
                    dp_complete();
                } else if (task_action->task.t.type == M_AUDTASK) {
                    run_rsp_microcode(rdram, &task_action->task, aspMain);
                } else if (task_action->task.t.type == M_NJPEGTASK) {
                    run_rsp_microcode(rdram, &task_action->task, njpgdspMain);
                } else {
                    fprintf(stderr, "Unknown task type: %" PRIu32 "\n", task_action->task.t.type);
                    assert(false);
                    std::quick_exit(EXIT_FAILURE);
                }
            } else if (const auto* swap_action = std::get_if<SwapBuffersAction>(&action)) {
                events_context.vi.current_buffer = events_context.vi.next_buffer;
                RT64UpdateScreen(swap_action->origin);
            }
        }

        // Handle events
        constexpr int max_events_per_frame = 16;
        SDL_Event cur_event;
        int i = 0;
        while (i++ < max_events_per_frame && SDL_PollEvent(&cur_event)) {
            sdl_event_filter(nullptr, &cur_event);
        }
        //SDL_PumpEvents();
    }
}

extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {
    events_context.vi.next_buffer = frameBufPtr;
    events_context.action_queue.enqueue(SwapBuffersAction{ osVirtualToPhysical(frameBufPtr) + 640 });
}

extern "C" PTR(void) osViGetNextFramebuffer() {
    return events_context.vi.next_buffer;
}

extern "C" PTR(void) osViGetCurrentFramebuffer() {
    return events_context.vi.current_buffer;
}

void Multilibultra::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {
    OSTask* task = TO_PTR(OSTask, task_);
    events_context.action_queue.enqueue(SpTaskAction{ *task });
}

void Multilibultra::send_si_message() {
    uint8_t* rdram = events_context.rdram;
    osSendMesg(PASS_RDRAM events_context.si.mq, events_context.si.msg, OS_MESG_NOBLOCK);
}

void Multilibultra::init_events(uint8_t* rdram, uint8_t* rom) {
    std::atomic_flag events_thread_ready;
    events_context.rdram = rdram;
    events_context.sp.thread = std::thread{ event_thread_func, rdram, rom, &events_thread_ready };
    
    // Wait for the event thread to be ready before continuing to prevent the game from
    // running before we're able to handle RSP tasks.
    events_thread_ready.wait(false);

    events_context.vi.thread = std::thread{ vi_thread_func };
}
