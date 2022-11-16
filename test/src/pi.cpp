#include <memory>
#include "recomp.h"
#include "../portultra/ultra64.h"

extern std::unique_ptr<uint8_t[]> rom;
extern size_t rom_size;

extern "C" void osCartRomInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osCreatePiManager_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

constexpr uint32_t rom_base = 0xB0000000;

extern "C" void osPiStartDma_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    uint32_t mb = ctx->r4;
    uint32_t pri = ctx->r5;
    uint32_t direction = ctx->r6;
    uint32_t devAddr = ctx->r7;
    uint32_t dramAddr = MEM_W(0x10, ctx->r29);
    uint32_t size = MEM_W(0x14, ctx->r29);
    uint32_t mq_ = MEM_W(0x18, ctx->r29);
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);

    printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    // TODO asynchronous transfer (will require preemption in the scheduler)
    // TODO this won't handle unaligned DMA
    memcpy(rdram + (dramAddr & 0x3FFFFFF), rom.get() + (devAddr | rom_base) - rom_base, size);

    // Send a message to the mq to indicate that the transfer completed
    osSendMesg(rdram, mq_, 0, OS_MESG_NOBLOCK);
}

extern "C" void osEPiStartDma_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osPiGetStatus_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ctx->r2 = 0;
}

extern "C" void osPiRawStartDma_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ;
}