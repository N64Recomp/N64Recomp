#ifdef _WIN32
#include <Windows.h>
#endif

#include <cstdio>
#include "recomp.h"

extern uint64_t start_time;


extern "C" void osVirtualToPhysical_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    uint32_t virtual_addr = ctx->r4;
    // TODO handle TLB mappings
    ctx->r2 = virtual_addr - 0x80000000;
}

extern "C" void osInvalDCache_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osInvalICache_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osWritebackDCache_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osWritebackDCacheAll_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osSetIntMask_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void __osDisableInt_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void __osRestoreInt_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
