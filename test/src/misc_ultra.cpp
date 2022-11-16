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

// Ticks per second
constexpr uint32_t counter_rate = 46'875'000;

extern "C" void osGetCount_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    // TODO move this to a more appropriate place
    int32_t count = 0;
#ifdef _WIN32
    SYSTEMTIME st;
    FILETIME ft;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &ft);

    uint64_t cur_time = ((uint64_t)ft.dwHighDateTime << 32) + ft.dwLowDateTime;
    uint64_t delta_100ns = cur_time - start_time;

    count = (delta_100ns * counter_rate) / (1'000'000'000 / 100);
#endif

    ctx->r2 = count;
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
