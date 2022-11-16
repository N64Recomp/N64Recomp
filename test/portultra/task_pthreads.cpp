#ifndef _WIN32

// #include <thread>
// #include <stdexcept>
// #include <atomic>

#include <pthread.h>
#include <signal.h>
#include <limits.h>

#include "ultra64.h"
#include "multilibultra.hpp"

constexpr int pause_thread_signum = SIGUSR1;

// void cleanup_current_thread(OSThread *t) {
//     debug_printf("Thread cleanup %d\n", t->id);

//     // delete t->context;
// }

void sig_handler(int signum, siginfo_t *info, void *context) {
    if (signum == pause_thread_signum) {
        OSThread *t = Multilibultra::this_thread();

        debug_printf("[Sig] Thread %d paused\n", t->id);

        // Wait until the thread is marked as running again
        t->context->running.wait(false);

        debug_printf("[Sig] Thread %d resumed\n", t->id);
    }
}

void Multilibultra::native_init(void) {
    // Set up a signal handler to capture pause signals
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO | SA_RESTART;
    sigact.sa_sigaction = sig_handler;

    sigaction(pause_thread_signum, &sigact, nullptr);
}

void Multilibultra::native_thread_init(OSThread *t) {
    debug_printf("[Native] Init thread %d\n", t->id);
}

void Multilibultra::pause_thread_native_impl(OSThread *t) {
    debug_printf("[Native] Pause thread %d\n", t->id);
    // Send a pause signal to the thread, which will trigger it to wait on its pause barrier in the signal handler
    pthread_kill(t->context->host_thread.native_handle(), pause_thread_signum);
}

void Multilibultra::resume_thread_native_impl(UNUSED OSThread *t) {
    debug_printf("[Native] Resume thread %d\n", t->id);
    // Nothing to do here
}

#endif