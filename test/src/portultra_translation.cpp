#include "../portultra/ultra64.h"
#include "recomp.h"

extern "C" void osInitialize_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    osInitialize();
}

extern "C" void osCreateThread_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    //printf("Creating thread 0x%08X\n", (uint32_t)ctx->r4);
    osCreateThread(rdram, (uint32_t)ctx->r4, (OSId)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7,
        (uint32_t)MEM_W(0x10, ctx->r29), (OSPri)MEM_W(0x14, ctx->r29));
}

extern "C" void osStartThread_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    //printf("Starting thread 0x%08X\n", (uint32_t)ctx->r4);
    osStartThread(rdram, (uint32_t)ctx->r4);
}

extern "C" void osSetThreadPri_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    osSetThreadPri(rdram, (uint32_t)ctx->r4, (OSPri)ctx->r5);
}

extern "C" void osCreateMesgQueue_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    osCreateMesgQueue(rdram, (uint32_t)ctx->r4, (uint32_t)ctx->r5, (s32)ctx->r6);
}

extern "C" void osRecvMesg_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = osRecvMesg(rdram, (uint32_t)ctx->r4, (uint32_t)ctx->r5, (s32)ctx->r6);
}

extern "C" void osSendMesg_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = osSendMesg(rdram, (uint32_t)ctx->r4, (OSMesg)ctx->r5, (s32)ctx->r6);
}

extern "C" void osJamMesg_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ctx->r2 = osJamMesg(rdram, (uint32_t)ctx->r4, (OSMesg)ctx->r5, (s32)ctx->r6);
}

extern "C" void osSetEventMesg_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    osSetEventMesg(rdram, (OSEvent)ctx->r4, (uint32_t)ctx->r5, (OSMesg)ctx->r6);
}
