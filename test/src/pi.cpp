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

void do_rom_read(uint8_t* rdram, gpr ram_address, uint32_t dev_address, size_t num_bytes) {
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
    gpr dramAddr = MEM_W(0x10, ctx->r29);
    uint32_t size = MEM_W(0x14, ctx->r29);
    PTR(OSMesgQueue) mq = MEM_W(0x18, ctx->r29);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    // TODO asynchronous transfer (will require preemption in the scheduler)
    // TODO this won't handle unaligned DMA
    do_rom_read(rdram, dramAddr, devAddr, size);

    //memcpy(rdram + (dramAddr & 0x3FFFFFF), rom.get() + (devAddr | rom_base) - rom_base, num_bytes);

    // Send a message to the mq to indicate that the transfer completed
    osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
}

struct OSIoMesgHdr {
    // These 3 reversed due to endianness
    u8 status;                 /* Return status */
    u8 pri;                    /* Message priority (High or Normal) */
    u16 type;                  /* Message type */
    PTR(OSMesgQueue) retQueue; /* Return message queue to notify I/O completion */
};

struct OSIoMesg {
    OSIoMesgHdr	hdr;    /* Message header */
    PTR(void) dramAddr;	/* RDRAM buffer address (DMA) */
    u32 devAddr;	    /* Device buffer address (DMA) */
    u32 size;		    /* DMA transfer size in bytes */
    u32 piHandle;	    /* PI device handle */
};

extern "C" void osEPiStartDma_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    OSIoMesg* mb = TO_PTR(OSIoMesg, ctx->r5);
    uint32_t direction = ctx->r6;
    uint32_t devAddr = mb->devAddr;
    gpr dramAddr = mb->dramAddr;
    uint32_t size = mb->size;
    PTR(OSMesgQueue) mq = mb->hdr.retQueue;

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    // TODO asynchronous transfer (will require preemption in the scheduler)
    // TODO this won't handle unaligned DMA
    do_rom_read(rdram, dramAddr, devAddr, size);

    //memcpy(rdram + (dramAddr & 0x3FFFFFF), rom.get() + (devAddr | rom_base) - rom_base, num_bytes);

    // Send a message to the mq to indicate that the transfer completed
    osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
}

extern "C" void osPiGetStatus_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ctx->r2 = 0;
}

extern "C" void osPiRawStartDma_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ;
}