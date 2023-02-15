#include "ultra64.h"
#include "multilibultra.hpp"

void Multilibultra::preinit(uint8_t* rdram, uint8_t* rom) {
    Multilibultra::set_main_thread();
    Multilibultra::init_events(rdram, rom);
    Multilibultra::init_timers(rdram);
    Multilibultra::init_audio();
    Multilibultra::save_init();
}

extern "C" void osInitialize() {
    Multilibultra::init_scheduler();
    Multilibultra::native_init();
}
