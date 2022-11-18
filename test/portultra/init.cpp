#include "ultra64.h"
#include "multilibultra.hpp"

void Multilibultra::preinit(uint8_t* rdram) {
    Multilibultra::set_main_thread();
    Multilibultra::init_events(rdram);
}

extern "C" void osInitialize() {
    Multilibultra::init_scheduler();
    Multilibultra::native_init();
}
