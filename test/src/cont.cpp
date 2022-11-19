#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osContInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osContStartReadData_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    Multilibultra::send_si_message();
}

struct OSContPad {
    u16 button;
    s8 stick_x;		/* -80 <= stick_x <= 80 */
    s8 stick_y;		/* -80 <= stick_y <= 80 */
    u8 errno_;
};

extern "C" void osContGetReadData_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    int32_t pad = (uint32_t)ctx->r4;

    // button
    MEM_H(0, pad) = 0;
    // stick_x
    MEM_B(2, pad) = 0;
    // stick_y
    MEM_B(3, pad) = 0;
    // errno
    MEM_B(4, pad) = 0;
}

extern "C" void osMotorInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osMotorStart_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osMotorStop_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
