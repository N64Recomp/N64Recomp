#ifndef __MULTILIBULTRA_HPP__
#define __MULTILIBULTRA_HPP__

#include <thread>
#include <atomic>
#include <mutex>

#include "ultra64.h"
#include "platform_specific.h"

struct UltraThreadContext {
    std::thread host_thread;
    std::atomic_bool running;
    std::atomic_bool initialized;
};

namespace Multilibultra {

void native_init();
void init_scheduler();
void native_thread_init(OSThread *t);
void set_self_paused(RDRAM_ARG1);
void wait_for_resumed(RDRAM_ARG1);
void swap_to_thread(RDRAM_ARG OSThread *to);
void pause_thread_impl(OSThread *t);
void pause_thread_native_impl(OSThread *t);
void resume_thread_impl(OSThread *t);
void resume_thread_native_impl(OSThread *t);
void schedule_running_thread(OSThread *t);
void stop_thread(OSThread *t);
void pause_self(RDRAM_ARG1);
void cleanup_thread(OSThread *t);
PTR(OSThread) this_thread();
void disable_preemption();
void enable_preemption();
void notify_scheduler();
void reprioritize_thread(OSThread *t, OSPri pri);
void set_main_thread();

class preemption_guard {
public:
    preemption_guard();
    ~preemption_guard();
private:
    std::lock_guard<std::mutex> lock;
};

} // namespace Multilibultra

#define debug_printf(...) printf(__VA_ARGS__);
//#define debug_printf(...)

#endif
