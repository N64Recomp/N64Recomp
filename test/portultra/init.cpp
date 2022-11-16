#include "ultra64.h"
#include "multilibultra.hpp"

extern "C" void osInitialize() {
    Multilibultra::init_scheduler();
    Multilibultra::native_init();
}
