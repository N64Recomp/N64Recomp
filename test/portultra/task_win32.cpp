#include <Windows.h>

#include "ultra64.h"
#include "multilibultra.hpp"

extern "C" unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000);
    return 0;
}

void Multilibultra::native_init(void) {
}

void Multilibultra::native_thread_init(OSThread *t) {
    debug_printf("[Native] Init thread %d\n", t->id);
}

void Multilibultra::pause_thread_native_impl(OSThread *t) {
    debug_printf("[Native] Pause thread %d\n", t->id);
    // Pause the thread via the win32 API
    SuspendThread(t->context->host_thread.native_handle());
    // Perform a synchronous action to ensure that the thread is suspended
    // see: https://devblogs.microsoft.com/oldnewthing/20150205-00/?p=44743
    CONTEXT threadContext;
    GetThreadContext(t->context->host_thread.native_handle(), &threadContext);
}

void Multilibultra::resume_thread_native_impl(UNUSED OSThread *t) {
    debug_printf("[Native] Resume thread %d\n", t->id);
    // Resume the thread
    ResumeThread(t->context->host_thread.native_handle());
}
