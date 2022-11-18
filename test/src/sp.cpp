#include <cstdio>
#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osSpTaskLoad_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osSpTaskStartGo_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    //printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    if (task->t.type == M_GFXTASK) {
        printf("[sp] Gfx task: %08X\n", (uint32_t)ctx->r4);
    } else if (task->t.type == M_AUDTASK) {
        printf("[sp] Audio task: %08X\n", (uint32_t)ctx->r4);
    }
    Multilibultra::submit_rsp_task(rdram, ctx->r4);
}

extern "C" void osSpTaskYield_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osSpTaskYielded_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void __osSpSetPc_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
