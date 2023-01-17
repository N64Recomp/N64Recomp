#include <thread>
#include <variant>
#include <set>
#include "blockingconcurrentqueue.h"

#include "ultra64.h"
#include "multilibultra.hpp"
#include "recomp.h"

// Start time for the program
static std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
// Game speed multiplier (1 means no speedup)
constexpr uint32_t speed_multiplier = 1;
// N64 CPU counter ticks per millisecond
constexpr uint32_t counter_per_ms = 46'875 * speed_multiplier;

struct OSTimer {
    PTR(OSTimer) unused1;
    PTR(OSTimer) unused2;
    OSTime interval;
    OSTime timestamp;
    PTR(OSMesgQueue) mq;
    OSMesg msg;
};

struct AddTimerAction {
    PTR(OSTask) timer;
};

struct RemoveTimerAction {
    PTR(OSTimer) timer;
};

using Action = std::variant<AddTimerAction, RemoveTimerAction>;

struct {
    std::thread thread;
    moodycamel::BlockingConcurrentQueue<Action> action_queue{};
} timer_context;

uint64_t duration_to_ticks(std::chrono::system_clock::duration duration) {
    uint64_t delta_micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    // More accurate than using a floating point timer, will only overflow after running for 12.47 years
    // Units: (micros * (counts/millis)) / (micros/millis) = counts
    uint64_t total_count = (delta_micros * counter_per_ms) / 1000;

    return total_count;
}

std::chrono::microseconds ticks_to_duration(uint64_t ticks) {
    using namespace std::chrono_literals;
    return ticks * 1000us / counter_per_ms;
}

std::chrono::system_clock::time_point ticks_to_timepoint(uint64_t ticks) {
    return start + ticks_to_duration(ticks);
}

uint64_t time_now() {
    return duration_to_ticks(std::chrono::system_clock::now() - start);
}

void timer_thread(RDRAM_ARG1) {
    // Lambda comparator function to keep the set ordered
    auto timer_sort = [PASS_RDRAM1](PTR(OSTimer) a_, PTR(OSTimer) b_) {
        OSTimer* a = TO_PTR(OSTimer, a_);
        OSTimer* b = TO_PTR(OSTimer, b_);

        // Order by timestamp if the timers have different timestamps
        if (a->timestamp != b->timestamp) {
            return a->timestamp < b->timestamp;
        }

        // If they have the exact same timestamp then order by address instead
        return a < b;
    };

    // Ordered set of timers that are currently active
    std::set<PTR(OSTimer), decltype(timer_sort)> active_timers{timer_sort};
    
    // Lambda to process a timer action to handle adding and removing timers
    auto process_timer_action = [&](const Action& action) {
        // Determine the action type and act on it
        if (const auto* add_action = std::get_if<AddTimerAction>(&action)) {
            active_timers.insert(add_action->timer);
        } else if (const auto* remove_action = std::get_if<RemoveTimerAction>(&action)) {
            active_timers.erase(remove_action->timer);
        }
    };

    while (true) {
        // Empty the action queue
        Action cur_action;
        while (timer_context.action_queue.try_dequeue(cur_action)) {
            process_timer_action(cur_action);
        }

        // If there's no timer to act on, wait for one to come in from the action queue
        while (active_timers.empty()) {
            timer_context.action_queue.wait_dequeue(cur_action);
            process_timer_action(cur_action);
        }

        // Get the timer that's closest to running out
        PTR(OSTimer) cur_timer_ = *active_timers.begin();
        OSTimer* cur_timer = TO_PTR(OSTimer, cur_timer_);

        // Remove the timer from the queue (it may get readded if waiting is interrupted)
        active_timers.erase(cur_timer_);

        // Determine how long to wait to reach the timer's timestamp
        auto wait_duration = ticks_to_timepoint(cur_timer->timestamp) - std::chrono::system_clock::now();
        auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(wait_duration);

        // Wait for either the duration to complete or a new action to come through
        if (timer_context.action_queue.wait_dequeue_timed(cur_action, wait_duration)) {
            // Timer was interrupted by a new action 
            // Add the current timer back to the queue (done first in case the action is to remove this timer)
            active_timers.insert(cur_timer_);
            // Process the new action
            process_timer_action(cur_action);
        } else {
            // Waiting for the timer completed, so send the timer's message to its message queue
            osSendMesg(PASS_RDRAM cur_timer->mq, cur_timer->msg, OS_MESG_NOBLOCK);
            // If the timer has a specified interval then reload it with that value
            if (cur_timer->interval != 0) {
                cur_timer->timestamp = cur_timer->interval + time_now();
                active_timers.insert(cur_timer_);
            }
        }
    }
}

void Multilibultra::init_timers(RDRAM_ARG1) {
    timer_context.thread = std::thread{ timer_thread, PASS_RDRAM1 };
}

uint32_t Multilibultra::get_speed_multiplier() {
    return speed_multiplier;
}

std::chrono::system_clock::time_point Multilibultra::get_start() {
    return start;
}

std::chrono::system_clock::duration Multilibultra::time_since_start() {
    return std::chrono::system_clock::now() - start;
}

extern "C" u32 osGetCount() {
    uint64_t total_count = time_now();

    // Allow for overflows, which is how osGetCount behaves
    return (uint32_t)total_count;
}

extern "C" OSTime osGetTime() {
    uint64_t total_count = time_now();

    return total_count;
}

extern "C" int osSetTimer(RDRAM_ARG PTR(OSTimer) t_, OSTime countdown, OSTime interval, PTR(OSMesgQueue) mq, OSMesg msg) {
    OSTimer* t = TO_PTR(OSTimer, t_);

    // Determine the time when this timer will trigger off
    if (countdown == 0) {
        // Set the timestamp based on the interval
        t->timestamp = interval + time_now();
    } else {
        t->timestamp = countdown + time_now();
    }
    t->interval = interval;
    t->mq = mq;
    t->msg = msg;

    timer_context.action_queue.enqueue(AddTimerAction{ t_ });

    return 0;
}

extern "C" int osStopTimer(RDRAM_ARG PTR(OSTimer) t_) {
    timer_context.action_queue.enqueue(RemoveTimerAction{ t_ });

    // TODO don't blindly return 0 here; requires some response from the timer thread to know what the returned value was
    return 0;
}
