#include <thread>
#include <queue>
#include <atomic>
#include <vector>

#include "multilibultra.hpp"

class OSThreadComparator {
public:
    bool operator() (OSThread *a, OSThread *b) const {
        return a->priority < b->priority;
    }
};

class thread_queue_t : public std::priority_queue<OSThread*, std::vector<OSThread*>, OSThreadComparator> {
public:
    // TODO comment this
    bool remove(const OSThread* value) {
        auto it = std::find(this->c.begin(), this->c.end(), value);
    
        if (it == this->c.end()) {
            return false;
        }

        if (it == this->c.begin()) {
            // deque the top element
            this->pop();
        } else {
            // remove element and re-heap
            this->c.erase(it);
            std::make_heap(this->c.begin(), this->c.end(), this->comp);
        }
        
        return true;
    }
};

static struct {
    std::vector<OSThread*> to_schedule;
    std::vector<OSThread*> to_stop;
    std::vector<OSThread*> to_cleanup;
    std::vector<std::pair<OSThread*, OSPri>> to_reprioritize;
    std::mutex mutex;
    // OSThread* running_thread;
    std::atomic_int notify_count;
    std::atomic_int action_count;

    bool can_preempt;
    std::mutex premption_mutex;
} scheduler_context{};

void handle_thread_queueing(thread_queue_t& running_thread_queue) {
    std::lock_guard lock{scheduler_context.mutex};

    if (!scheduler_context.to_schedule.empty()) {
        OSThread* to_schedule = scheduler_context.to_schedule.back();
        scheduler_context.to_schedule.pop_back();
        scheduler_context.action_count.fetch_sub(1);
        debug_printf("[Scheduler] Scheduling thread %d\n", to_schedule->id);
        running_thread_queue.push(to_schedule);
    }
}

void handle_thread_stopping(thread_queue_t& running_thread_queue) {
    std::lock_guard lock{scheduler_context.mutex};

    while (!scheduler_context.to_stop.empty()) {
        OSThread* to_stop = scheduler_context.to_stop.back();
        scheduler_context.to_stop.pop_back();
        scheduler_context.action_count.fetch_sub(1);
        debug_printf("[Scheduler] Stopping thread %d\n", to_stop->id);
        running_thread_queue.remove(to_stop);
    }
}

void handle_thread_cleanup(thread_queue_t& running_thread_queue) {
    std::lock_guard lock{scheduler_context.mutex};

    while (!scheduler_context.to_cleanup.empty()) {
        OSThread* to_cleanup = scheduler_context.to_cleanup.back();
        scheduler_context.to_cleanup.pop_back();
        scheduler_context.action_count.fetch_sub(1);
        
        debug_printf("[Scheduler] Destroying thread %d\n", to_cleanup->id);
        running_thread_queue.remove(to_cleanup);
        to_cleanup->context->host_thread.join();
        delete to_cleanup->context;
    }
}

void handle_thread_reprioritization(thread_queue_t& running_thread_queue) {
    std::lock_guard lock{scheduler_context.mutex};

    while (!scheduler_context.to_reprioritize.empty()) {
        const std::pair<OSThread*, OSPri> to_reprioritize = scheduler_context.to_reprioritize.back();
        scheduler_context.to_reprioritize.pop_back();
        scheduler_context.action_count.fetch_sub(1);
        
        debug_printf("[Scheduler] Reprioritizing thread %d to %d\n", to_reprioritize.first->id, to_reprioritize.second);
        running_thread_queue.remove(to_reprioritize.first);
        to_reprioritize.first->priority = to_reprioritize.second;
        running_thread_queue.push(to_reprioritize.first);
    }
}

void handle_scheduler_notifications() {
    std::lock_guard lock{scheduler_context.mutex};
    int32_t notify_count = scheduler_context.notify_count.exchange(0);
    if (notify_count) {
        debug_printf("Received %d notifications\n", notify_count);
        scheduler_context.action_count.fetch_sub(notify_count);
    }
}

void swap_running_thread(thread_queue_t& running_thread_queue, OSThread*& cur_running_thread) {
    OSThread* new_running_thread = running_thread_queue.top();
    if (cur_running_thread != new_running_thread) {
        if (cur_running_thread && cur_running_thread->state == OSThreadState::RUNNING) {
            debug_printf("[Scheduler] Switching execution from thread %d (%d) to thread %d (%d)\n",
                cur_running_thread->id, cur_running_thread->priority,
                new_running_thread->id, new_running_thread->priority);
            Multilibultra::pause_thread_impl(cur_running_thread);
        } else {
            debug_printf("[Scheduler] Switching execution to thread %d (%d)\n", new_running_thread->id, new_running_thread->priority);
        }
        Multilibultra::resume_thread_impl(new_running_thread);
        cur_running_thread = new_running_thread;
    } else if (cur_running_thread && cur_running_thread->state != OSThreadState::RUNNING) {
        Multilibultra::resume_thread_impl(cur_running_thread);
    }
}

void scheduler_func() {
    thread_queue_t running_thread_queue{};
    OSThread* cur_running_thread = nullptr;

    while (true) {
        OSThread* old_running_thread = cur_running_thread;
        scheduler_context.action_count.wait(0);

        std::lock_guard lock{scheduler_context.premption_mutex};
        
        // Handle notifications
        handle_scheduler_notifications();

        // Handle stopping threads
        handle_thread_stopping(running_thread_queue);

        // Handle cleaning up threads
        handle_thread_cleanup(running_thread_queue);

        // Handle queueing threads to run
        handle_thread_queueing(running_thread_queue);

        // Handle threads that have changed priority
        handle_thread_reprioritization(running_thread_queue);

        // Determine which thread to run, stopping the current running thread if necessary
        swap_running_thread(running_thread_queue, cur_running_thread);

        std::this_thread::yield();
        if (old_running_thread != cur_running_thread && old_running_thread && cur_running_thread) {
            debug_printf("[Scheduler] Swapped from Thread %d (%d) to Thread %d (%d)\n",
                old_running_thread->id, old_running_thread->priority, cur_running_thread->id, cur_running_thread->priority);
        }
    }
}

extern "C" void do_yield() {
    std::this_thread::yield();
}

namespace Multilibultra {

void init_scheduler() {
    scheduler_context.can_preempt = true;
    std::thread scheduler_thread{scheduler_func};
    scheduler_thread.detach();
}

void schedule_running_thread(OSThread *t) {
    debug_printf("[Scheduler] Queuing Thread %d to be scheduled\n", t->id);
    std::lock_guard lock{scheduler_context.mutex};
    scheduler_context.to_schedule.push_back(t);
    scheduler_context.action_count.fetch_add(1);
    scheduler_context.action_count.notify_all();
}

void swap_to_thread(RDRAM_ARG OSThread *to) {
    OSThread *self = TO_PTR(OSThread, Multilibultra::this_thread());
    debug_printf("[Scheduler] Scheduling swap from thread %d to %d\n", self->id, to->id);
    {
        std::lock_guard lock{scheduler_context.mutex};
        scheduler_context.to_schedule.push_back(to);
        Multilibultra::set_self_paused(PASS_RDRAM1);
        scheduler_context.action_count.fetch_add(1);
        scheduler_context.action_count.notify_all();
    }
    Multilibultra::wait_for_resumed(PASS_RDRAM1);
}

void reprioritize_thread(OSThread *t, OSPri pri) {
    debug_printf("[Scheduler] Adjusting Thread %d priority to %d\n", t->id, pri);
    std::lock_guard lock{scheduler_context.mutex};
    scheduler_context.to_reprioritize.emplace_back(t, pri);
    scheduler_context.action_count.fetch_add(1);
    scheduler_context.action_count.notify_all();
}

void pause_self(RDRAM_ARG1) {
    OSThread *self = TO_PTR(OSThread, Multilibultra::this_thread());
    debug_printf("[Scheduler] Thread %d pausing itself\n", self->id);
    {
        std::lock_guard lock{scheduler_context.mutex};
        Multilibultra::set_self_paused(PASS_RDRAM1);
        scheduler_context.to_stop.push_back(self);
        scheduler_context.action_count.fetch_add(1);
        scheduler_context.action_count.notify_all();
    }
    Multilibultra::wait_for_resumed(PASS_RDRAM1);
}

void stop_thread(OSThread *t) {
    debug_printf("[Scheduler] Queuing Thread %d to be stopped\n", t->id);
    {
        std::lock_guard lock{scheduler_context.mutex};
        scheduler_context.to_stop.push_back(t);
        scheduler_context.action_count.fetch_add(1);
        scheduler_context.action_count.notify_all();
    }
    Multilibultra::pause_thread_impl(t);
}

void cleanup_thread(OSThread *t) {
    std::lock_guard lock{scheduler_context.mutex};
    scheduler_context.to_cleanup.push_back(t);
    scheduler_context.action_count.fetch_add(1);
    scheduler_context.action_count.notify_all();
}

void disable_preemption() {
    scheduler_context.premption_mutex.lock();
    scheduler_context.can_preempt = false;
}

void enable_preemption() {
    scheduler_context.can_preempt = true;
    scheduler_context.premption_mutex.unlock();
}

// lock's constructor is called first, so can_preempt is set after locking
preemption_guard::preemption_guard() : lock{scheduler_context.premption_mutex} {
    scheduler_context.can_preempt = false;
}

// lock's destructor is called last, so can_preempt is set before unlocking
preemption_guard::~preemption_guard() {
    scheduler_context.can_preempt = true;
}

void notify_scheduler() {
    std::lock_guard lock{scheduler_context.mutex};
    scheduler_context.notify_count.fetch_add(1);
    scheduler_context.action_count.fetch_add(1);
    scheduler_context.action_count.notify_all();
}

}
