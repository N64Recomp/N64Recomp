#include "recomp.h"

extern "C" void osAiSetFrequency_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = ctx->r4;
}

static uint32_t ai_length = 0;

extern "C" void osAiSetNextBuffer_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ai_length = (uint32_t)ctx->r5;
    ctx->r2 = 0;
}

extern "C" void osAiGetLength_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = ai_length;
}

extern "C" void osAiGetStatus_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = 0x80000000;
}
