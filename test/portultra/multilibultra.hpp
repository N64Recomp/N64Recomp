#ifndef __MULTILIBULTRA_HPP__
#define __MULTILIBULTRA_HPP__

#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

#include "ultra64.h"
#include "platform_specific.h"

struct UltraThreadContext {
    std::thread host_thread;
    std::atomic_bool running;
    std::atomic_bool initialized;
};

namespace Multilibultra {

void preinit(uint8_t* rdram, uint8_t* rom);
void native_init();
void init_scheduler();
void init_events(uint8_t* rdram, uint8_t* rom);
void init_timers(RDRAM_ARG1);
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
bool is_game_thread();
void submit_rsp_task(RDRAM_ARG PTR(OSTask) task);
void send_si_message();
uint32_t get_speed_multiplier();
std::chrono::system_clock::time_point get_start();
std::chrono::system_clock::duration time_since_start();

class preemption_guard {
public:
    preemption_guard();
    ~preemption_guard();
private:
    std::lock_guard<std::mutex> lock;
};

} // namespace Multilibultra

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define debug_printf(...)
//#define debug_printf(...) printf(__VA_ARGS__);

#endif
