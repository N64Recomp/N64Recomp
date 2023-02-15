#include <memory>
#include "../portultra/ultra64.h"
#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osInitialize_recomp(uint8_t * rdram, recomp_context * ctx) {
    osInitialize();
}

extern "C" void __osInitialize_common_recomp(uint8_t * rdram, recomp_context * ctx) {
    osInitialize();
}

extern "C" void osCreateThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    osCreateThread(rdram, (int32_t)ctx->r4, (OSId)ctx->r5, (int32_t)ctx->r6, (int32_t)ctx->r7,
        (int32_t)MEM_W(0x10, ctx->r29), (OSPri)MEM_W(0x14, ctx->r29));
}

extern "C" void osStartThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    osStartThread(rdram, (int32_t)ctx->r4);
}

extern "C" void osStopThread_recomp(uint8_t * rdram, recomp_context * ctx) {
    osStopThread(rdram, (int32_t)ctx->r4);
}

extern "C" void osDestroyThread_recomp(uint8_t * rdram, recomp_context * ctx) {
    osDestroyThread(rdram, (int32_t)ctx->r4);
}

extern "C" void osSetThreadPri_recomp(uint8_t* rdram, recomp_context* ctx) {
    osSetThreadPri(rdram, (int32_t)ctx->r4, (OSPri)ctx->r5);
}

extern "C" void osGetThreadPri_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = osGetThreadPri(rdram, (int32_t)ctx->r4);
}

extern "C" void osGetThreadId_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = osGetThreadId(rdram, (int32_t)ctx->r4);
}

extern "C" void osCreateMesgQueue_recomp(uint8_t* rdram, recomp_context* ctx) {
    osCreateMesgQueue(rdram, (int32_t)ctx->r4, (int32_t)ctx->r5, (s32)ctx->r6);
}

extern "C" void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = osRecvMesg(rdram, (int32_t)ctx->r4, (int32_t)ctx->r5, (s32)ctx->r6);
}

extern "C" void osSendMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = osSendMesg(rdram, (int32_t)ctx->r4, (OSMesg)ctx->r5, (s32)ctx->r6);
}

extern "C" void osJamMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = osJamMesg(rdram, (int32_t)ctx->r4, (OSMesg)ctx->r5, (s32)ctx->r6);
}

extern "C" void osSetEventMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    osSetEventMesg(rdram, (OSEvent)ctx->r4, (int32_t)ctx->r5, (OSMesg)ctx->r6);
}

extern "C" void osViSetEvent_recomp(uint8_t * rdram, recomp_context * ctx) {
    osViSetEvent(rdram, (int32_t)ctx->r4, (OSMesg)ctx->r5, (u32)ctx->r6);
}

extern "C" void osGetCount_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = osGetCount();
}

extern "C" void osGetTime_recomp(uint8_t * rdram, recomp_context * ctx) {
    uint64_t total_count = osGetTime();
    ctx->r2 = (int32_t)(total_count >> 32);
    ctx->r3 = (int32_t)(total_count >> 0);
}

extern "C" void osSetTimer_recomp(uint8_t * rdram, recomp_context * ctx) {
    uint64_t countdown = ((uint64_t)(ctx->r6) << 32) | ((ctx->r7) & 0xFFFFFFFFu);
    uint64_t interval = load_doubleword(rdram, ctx->r29, 0x10);
    ctx->r2 = osSetTimer(rdram, (int32_t)ctx->r4, countdown, interval, (int32_t)MEM_W(0x18, ctx->r29), (OSMesg)MEM_W(0x1C, ctx->r29));
}

extern "C" void osStopTimer_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = osStopTimer(rdram, (int32_t)ctx->r4);
}

extern "C" void osVirtualToPhysical_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = osVirtualToPhysical((int32_t)ctx->r2);
}

extern "C" void osInvalDCache_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void osInvalICache_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void osWritebackDCache_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void osWritebackDCacheAll_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void osSetIntMask_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void __osDisableInt_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void __osRestoreInt_recomp(uint8_t * rdram, recomp_context * ctx) {
    ;
}

extern "C" void __osSetFpcCsr_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = 0;
}

// For the Mario Party games (not working)
//extern "C" void longjmp_recomp(uint8_t * rdram, recomp_context * ctx) {
//    RecompJmpBuf* buf = TO_PTR(RecompJmpBuf, ctx->r4);
//
//    // Check if this is a buffer that was set up with setjmp
//    if (buf->magic == SETJMP_MAGIC) {
//        // If so, longjmp to it
//        // Setjmp/longjmp does not work across threads, so verify that this buffer was made by this thread
//        assert(buf->owner == Multilibultra::this_thread());
//        longjmp(buf->storage->buffer, ctx->r5);
//    } else {
//        // Otherwise, check if it was one built manually by the game with $ra pointing to a function
//        gpr sp = MEM_W(0, ctx->r4);
//        gpr ra = MEM_W(4, ctx->r4);
//        ctx->r29 = sp;
//        recomp_func_t* target = LOOKUP_FUNC(ra);
//        if (target == nullptr) {
//            fprintf(stderr, "Failed to find function for manual longjmp\n");
//            std::quick_exit(EXIT_FAILURE);
//        }
//        target(rdram, ctx);
//
//        // TODO kill this thread if the target function returns
//        assert(false);
//    }
//}
//
//#undef setjmp_recomp
//extern "C" void setjmp_recomp(uint8_t * rdram, recomp_context * ctx) {
//    fprintf(stderr, "Program called setjmp_recomp\n");
//    std::quick_exit(EXIT_FAILURE);
//}
//
//extern "C" int32_t osGetThreadEx(void) {
//    return Multilibultra::this_thread();
//}
