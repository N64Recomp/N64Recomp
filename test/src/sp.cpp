#include <cstdio>
#include "recomp.h"

extern "C" void osSpTaskLoad_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osSpTaskStartGo_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
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
