#include <memory>
#include <fstream>
#include <array>
#include "recomp.h"
#include "../portultra/ultra64.h"
#include "../portultra/multilibultra.hpp"

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

struct OSPiHandle {
    PTR(OSPiHandle_s)    unused;        /* point to next handle on the table */
    // These four members reversed due to endianness
    u8                   relDuration;   /* domain release duration */
    u8                   pageSize;      /* domain page size */
    u8                   latency;       /* domain latency */
    u8                   type;          /* DEVICE_TYPE_BULK for disk */
    // These three members reversed due to endianness
    u16                  padding;       /* struct alignment padding */
    u8                   domain;        /* which domain */
    u8                   pulse;         /* domain pulse width */
    u32                  baseAddress;   /* Domain address */
    u32                  speed;         /* for roms only */
    /* The following are "private" elements" */
    u32                  transferInfo[18];  /* for disk only */
};

// Flashram occupies the same physical address as sram, but that issue is avoided because libultra exposes
// a high-level interface for flashram. Because that high-level interface is reimplemented, low level accesses
// that involve physical addresses don't need to be handled for flashram.
constexpr uint32_t sram_base = 0x08000000;
constexpr uint32_t rom_base = 0x10000000;

constexpr uint32_t k1_to_phys(uint32_t addr) {
    return addr & 0x1FFFFFFF;
}

constexpr uint32_t phys_to_k1(uint32_t addr) {
    return addr | 0xA0000000;
}

// We need a place in rdram to hold the cart handle, so pick an address in extended rdram
constexpr int32_t cart_handle = 0x80800000;

extern std::unique_ptr<uint8_t[]> rom;
extern size_t rom_size;

extern "C" void osCartRomInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, cart_handle);
    handle->type = 0; // cart
    handle->baseAddress = phys_to_k1(rom_base);
    handle->domain = 0;

    ctx->r2 = (gpr)cart_handle;
}

extern "C" void osCreatePiManager_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

void do_rom_read(uint8_t* rdram, gpr ram_address, uint32_t physical_addr, size_t num_bytes) {
    // TODO use word copies when possible
    uint8_t* rom_addr = rom.get() + physical_addr - rom_base;
    for (size_t i = 0; i < num_bytes; i++) {
        MEM_B(i, ram_address) = *rom_addr;
        rom_addr++;
    }
}

std::array<char, 0x20000> save_buffer;
const char save_filename[] = "save.bin";

void save_write(uint8_t* restrict rdram, gpr rdram_address, uint32_t offset, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        save_buffer[offset + i] = MEM_B(i, rdram_address);
    }
    std::ofstream save_file{ save_filename, std::ios_base::binary };

    if (save_file.good()) {
        save_file.write(save_buffer.data(), save_buffer.size());
    } else {
        fprintf(stderr, "Failed to save!\n");
        std::exit(EXIT_FAILURE);
    }
}

void save_read(uint8_t* restrict rdram, gpr rdram_address, uint32_t offset, uint32_t count) {
    for (size_t i = 0; i < count; i++) {
        MEM_B(i, rdram_address) = save_buffer[offset + i];
    }
}

void Multilibultra::save_init() {
    std::ifstream save_file{ save_filename, std::ios_base::binary };

    if (save_file.good()) {
        save_file.read(save_buffer.data(), save_buffer.size());
    } else {
        save_buffer.fill(0);
    }
}

void do_dma(uint8_t* restrict rdram, PTR(OSMesgQueue) mq, gpr rdram_address, uint32_t physical_addr, uint32_t size, uint32_t direction) {
    // TODO asynchronous transfer
    // TODO implement unaligned DMA correctly
    if (direction == 0) {
        if (physical_addr >= rom_base) {
            // read cart rom
            do_rom_read(rdram, rdram_address, physical_addr, size);

            // Send a message to the mq to indicate that the transfer completed
            osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
        } else if (physical_addr >= sram_base) {
            // read sram
            save_read(rdram, rdram_address, physical_addr - sram_base, size);

            // Send a message to the mq to indicate that the transfer completed
            osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
        } else {
            fprintf(stderr, "[WARN] PI DMA read from unknown region, phys address 0x%08X\n", physical_addr);
        }
    } else {
        if (physical_addr >= rom_base) {
            // write cart rom
            throw std::runtime_error("ROM DMA write unimplemented");
        } else if (physical_addr >= sram_base) {
            // write sram
            save_write(rdram, rdram_address, physical_addr - sram_base, size);

            // Send a message to the mq to indicate that the transfer completed
            osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
        } else {
            fprintf(stderr, "[WARN] PI DMA write to unknown region, phys address 0x%08X\n", physical_addr);
        }
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
    uint32_t physical_addr = k1_to_phys(devAddr);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    do_dma(rdram, mq, dramAddr, physical_addr, size, direction);

    ctx->r2 = 0;
}

extern "C" void osEPiStartDma_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    OSIoMesg* mb = TO_PTR(OSIoMesg, ctx->r5);
    uint32_t direction = ctx->r6;
    uint32_t devAddr = handle->baseAddress | mb->devAddr;
    gpr dramAddr = mb->dramAddr;
    uint32_t size = mb->size;
    PTR(OSMesgQueue) mq = mb->hdr.retQueue;
    uint32_t physical_addr = k1_to_phys(devAddr);

    debug_printf("[pi] DMA from 0x%08X into 0x%08X of size 0x%08X\n", devAddr, dramAddr, size);

    do_dma(rdram, mq, dramAddr, physical_addr, size, direction);

    ctx->r2 = 0;
}

extern "C" void osEPiReadIo_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    uint32_t devAddr = handle->baseAddress | ctx->r5;
    gpr dramAddr = ctx->r6;
    uint32_t physical_addr = k1_to_phys(devAddr);

    if (physical_addr > rom_base) {
        // cart rom
        do_rom_read(rdram, dramAddr, physical_addr, sizeof(uint32_t));
    } else {
        // sram
        assert(false && "SRAM ReadIo unimplemented");
    }

    ctx->r2 = 0;
}

extern "C" void osPiGetStatus_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ctx->r2 = 0;
}

extern "C" void osPiRawStartDma_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ctx->r2 = 0;
}
