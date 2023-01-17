#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osViSetYScale_recomp(uint8_t* restrict rdram, recomp_context * restrict ctx) {
    ;
}

extern "C" void osViSetXScale_recomp(uint8_t* restrict rdram, recomp_context * restrict ctx) {
    ;
}

extern "C" void osCreateViManager_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osViBlack_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osViSetSpecialFeatures_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osViGetCurrentFramebuffer_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = (gpr)(int32_t)osViGetCurrentFramebuffer();
}

extern "C" void osViGetNextFramebuffer_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = (gpr)(int32_t)osViGetNextFramebuffer();
}

extern "C" void osViSwapBuffer_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    osViSwapBuffer(rdram, (int32_t)ctx->r4);
}

extern "C" void osViSetMode_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
