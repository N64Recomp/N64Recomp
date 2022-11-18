#include <memory>
#include "recomp.h"
#include "../portultra/ultra64.h"
#include "../portultra/multilibultra.hpp"

extern std::unique_ptr<uint8_t[]> rom;
extern size_t rom_size;

extern "C" void osCartRomInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osCreatePiManager_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

constexpr uint32_t rom_base = 0xB0000000;

void do_rom_read(uint8_t* rdram, uint32_t ram_address, uint32_t dev_address, size_t num_bytes) {
    // TODO use word copies when possible
    uint8_t* rom_addr = rom.get() + (dev_address | rom_base) - rom_base;
    for (size_t i = 0; i < num_bytes; i++) {
        MEM_B(i, ram_address) = *rom_addr;
        rom_addr++;
    }
}

extern "C" void osPiStartDma_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    uint32_t mb = ctx->r4;
    uint32_t pri = ctx->r5;
    uint32_t direction = ctx->r6;
    uint32_t devAddr = ctx->r7;
    uint32_t dramAddr = MEM_W(0x10, ctx->r29);
    uint32_t size = MEM_W(0x14, ctx->r29);
    uint32_t mq_ = MEM_W(0x18, ctx->r29);
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    // TODO asynchronous transfer (will require preemption in the scheduler)
    // TODO this won't handle unaligned DMA
    do_rom_read(rdram, dramAddr, devAddr, size);

    //memcpy(rdram + (dramAddr & 0x3FFFFFF), rom.get() + (devAddr | rom_base) - rom_base, num_bytes);

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