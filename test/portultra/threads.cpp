#include <cstdio>
#include <thread>

#include "ultra64.h"
#include "multilibultra.hpp"

extern "C" void bootproc();

thread_local bool is_main_thread = false;
// Whether this thread is part of the game (i.e. the start thread or one spawned by osCreateThread)
thread_local bool is_game_thread = false;
thread_local PTR(OSThread) thread_self = NULLPTR;

void Multilibultra::set_main_thread() {
    ::is_game_thread = true;
    is_main_thread = true;
}

bool Multilibultra::is_game_thread() {
    return ::is_game_thread;
}

#if 0
int main(int argc, char** argv) {
    Multilibultra::set_main_thread();

    bootproc();
}
#endif

#if 1
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg);
#else
#define run_thread_function(func, sp, arg) func(arg)
#endif

static void _thread_func(RDRAM_ARG PTR(OSThread) self_, PTR(thread_func_t) entrypoint, PTR(void) arg) {
    OSThread *self = TO_PTR(OSThread, self_);
    debug_printf("[Thread] Thread created: %d\n", self->id);
    thread_self = self_;
    is_game_thread = true;

    // Perform any necessary native thread initialization.
    Multilibultra::native_thread_init(self);

    // Set initialized to false to indicate that this thread can be started.
    self->context->initialized.store(true);
    self->context->initialized.notify_all();

    debug_printf("[Thread] Thread waiting to be started: %d\n", self->id);

    // Wait until the thread is marked as running.
    Multilibultra::set_self_paused(PASS_RDRAM1);
    Multilibultra::wait_for_resumed(PASS_RDRAM1);

    debug_printf("[Thread] Thread started: %d\n", self->id);

    // Run the thread's function with the provided argument.
    run_thread_function(PASS_RDRAM entrypoint, self->sp, arg);

    // Dispose of this thread after it completes.
    Multilibultra::cleanup_thread(self);
}

extern "C" void osStartThread(RDRAM_ARG PTR(OSThread) t_) {
    OSThread* t = TO_PTR(OSThread, t_);
    debug_printf("[os] Start Thread %d\n", t->id);

    // Wait until the thread is initialized to indicate that it's action_queued to be started.
    t->context->initialized.wait(false);

    debug_printf("[os] Thread %d is ready to be started\n", t->id);

    if (thread_self && (t->priority > TO_PTR(OSThread, thread_self)->priority)) {
        Multilibultra::swap_to_thread(PASS_RDRAM t);
    } else {
        Multilibultra::schedule_running_thread(t);
    }

    // The main thread "becomes" the first thread started, so join on it and exit after it completes.
    if (is_main_thread) {
        t->context->host_thread.join();
        std::exit(EXIT_SUCCESS);
    }
}

extern "C" void osCreateThread(RDRAM_ARG PTR(OSThread) t_, OSId id, PTR(thread_func_t) entrypoint, PTR(void) arg, PTR(void) sp, OSPri pri) {
    debug_printf("[os] Create Thread %d\n", id);
    OSThread *t = TO_PTR(OSThread, t_);
    
    t->next = NULLPTR;
    t->priority = pri;
    t->id = id;
    t->state = OSThreadState::PAUSED;
    t->sp = sp;

    // Spawn a new thread, which will immediately pause itself and wait until it's been started.
    t->context = new UltraThreadContext{};
    t->context->initialized.store(false);
    t->context->running.store(false);

    t->context->host_thread = std::thread{_thread_func, PASS_RDRAM t_, entrypoint, arg};
}

extern "C" void osSetThreadPri(RDRAM_ARG PTR(OSThread) t, OSPri pri) {
    if (t == NULLPTR) {
        t = thread_self;
    }
    bool pause_self = false;
    if (pri > TO_PTR(OSThread, thread_self)->priority) {
        pause_self = true;
        Multilibultra::set_self_paused(PASS_RDRAM1);
    } else if (t == thread_self && pri < TO_PTR(OSThread, thread_self)->priority) {
        pause_self = true;
        Multilibultra::set_self_paused(PASS_RDRAM1);
    }
    Multilibultra::reprioritize_thread(TO_PTR(OSThread, t), pri);
    if (pause_self) {
        Multilibultra::wait_for_resumed(PASS_RDRAM1);
    }
}

// TODO yield thread, need a stable priority queue in the scheduler

void Multilibultra::set_self_paused(RDRAM_ARG1) {
    debug_printf("[Thread] Thread pausing itself: %d\n", TO_PTR(OSThread, thread_self)->id);
    TO_PTR(OSThread, thread_self)->state = OSThreadState::PAUSED;
    TO_PTR(OSThread, thread_self)->context->running.store(false);
    TO_PTR(OSThread, thread_self)->context->running.notify_all();
}

void Multilibultra::wait_for_resumed(RDRAM_ARG1) {
    TO_PTR(OSThread, thread_self)->context->running.wait(false);
}

void Multilibultra::pause_thread_impl(OSThread* t) {
    t->state = OSThreadState::PREEMPTED;
    t->context->running.store(false);
    t->context->running.notify_all();
    Multilibultra::pause_thread_native_impl(t);
}

void Multilibultra::resume_thread_impl(OSThread *t) {
    if (t->state == OSThreadState::PREEMPTED) {
        Multilibultra::resume_thread_native_impl(t);
    }
    t->state = OSThreadState::RUNNING;
    t->context->running.store(true);
    t->context->running.notify_all();
}

PTR(OSThread) Multilibultra::this_thread() {
    return thread_self;
}
