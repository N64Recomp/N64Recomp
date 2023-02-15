#include "../portultra/multilibultra.hpp"
#include "recomp.h"

static int max_controllers = 0;

extern "C" void osContInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr bitpattern = ctx->r5;
    gpr status = ctx->r6;

    // Set bit 0 to indicate that controller 0 is present
    MEM_B(0, bitpattern) = 0x01;

    // Mark controller 0 as present
    MEM_H(0, status) = 0x0005; // type: CONT_TYPE_NORMAL (from joybus)
    MEM_B(2, status) = 0x00; // status: 0 (from joybus)
    MEM_B(3, status) = 0x00; // errno: 0 (from libultra)

    max_controllers = 4;

    // Mark controllers 1-3 as not connected
    for (size_t controller = 1; controller < max_controllers; controller++) {
        // Libultra doesn't write status or type for absent controllers
        MEM_B(4 * controller + 3, status) = 0x80 >> 4; // errno: CONT_NO_RESPONSE_ERROR >> 4
    }

    ctx->r2 = 0;
}

extern "C" void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
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

extern "C" void osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    int32_t pad = (int32_t)ctx->r4;

    if (max_controllers > 0) {
        // button
        MEM_H(0, pad) = button;
        // stick_x
        MEM_B(2, pad) = stick_x;
        // stick_y
        MEM_B(3, pad) = stick_y;
        // errno
        MEM_B(4, pad) = 0;
    }
    for (int controller = 1; controller < max_controllers; controller++) {
        MEM_B(6 * controller + 4, pad) = 0x80 >> 4; // errno: CONT_NO_RESPONSE_ERROR >> 4
    }
}

extern "C" void osContStartQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    Multilibultra::send_si_message();
}

extern "C" void osContGetQuery_recomp(uint8_t * rdram, recomp_context * ctx) {
    gpr status = ctx->r4;

    // Mark controller 0 as present
    MEM_H(0, status) = 0x0005; // type: CONT_TYPE_NORMAL (from joybus)
    MEM_B(2, status) = 0x00; // status: 0 (from joybus)
    MEM_B(3, status) = 0x00; // errno: 0 (from libultra)

    // Mark controllers 1-3 as not connected
    for (size_t controller = 1; controller < max_controllers; controller++) {
        // Libultra doesn't write status or type for absent controllers
        MEM_B(4 * controller + 3, status) = 0x80 >> 4; // errno: CONT_NO_RESPONSE_ERROR >> 4
    }
}

extern "C" void osContSetCh_recomp(uint8_t* rdram, recomp_context* ctx) {
    max_controllers = std::min((unsigned int)ctx->r4, 4u);
    ctx->r2 = 0;
}

extern "C" void __osMotorAccess_recomp(uint8_t* rdram, recomp_context* ctx) {

}

extern "C" void osMotorInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    ;
}

extern "C" void osMotorStart_recomp(uint8_t* rdram, recomp_context* ctx) {
    ;
}

extern "C" void osMotorStop_recomp(uint8_t* rdram, recomp_context* ctx) {
    ;
}
