#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osContInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    gpr bitpattern = ctx->r5;
    gpr status = ctx->r6;
    MEM_B(0, bitpattern) = 0x01;

    MEM_H(0, status) = 0x0005; // CONT_TYPE_NORMAL
    MEM_B(2, status) = 0; // controller status
    MEM_B(3, status) = 0; // controller errno

    // Write CHNL_ERR_NORESP for the other controllers
    for (size_t controller = 1; controller < 4; controller++) {
        MEM_B(4 * controller + 3, status) = 0x80;
    }

    ctx->r2 = 0;
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

int button = 0;
int stick_x = 0;
int stick_y = 0;

void press_button(int button) {

}

void release_button(int button) {

}

extern "C" void osContGetReadData_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    int32_t pad = (uint32_t)ctx->r4;

    // button
    MEM_H(0, pad) = button;
    // stick_x
    MEM_B(2, pad) = stick_x;
    // stick_y
    MEM_B(3, pad) = stick_y;
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
