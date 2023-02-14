#ifndef __RSP_H__
#define __RSP_H__

#include "rsp_vu.h"
#include "recomp.h"

enum class RspExitReason {
    Invalid,
    Broke,
    ImemOverrun,
    UnhandledJumpTarget
};

extern uint8_t dmem[];
extern uint16_t rspReciprocals[512];
extern uint16_t rspInverseSquareRoots[512];

#define RSP_MEM_W(offset, addr) \
    (*reinterpret_cast<uint32_t*>(dmem + (offset) + (addr)))

#define RSP_MEM_H(offset, addr) \
    (*reinterpret_cast<int16_t*>(dmem + (((offset) + (addr)) ^ 2)))

#define RSP_MEM_HU(offset, addr) \
    (*reinterpret_cast<uint16_t*>(dmem + (((offset) + (addr)) ^ 2)))

#define RSP_MEM_B(offset, addr) \
    (*reinterpret_cast<int8_t*>(dmem + (((offset) + (addr)) ^ 3)))

#define RSP_MEM_BU(offset, addr) \
    (*reinterpret_cast<uint8_t*>(dmem + (((offset) + (addr)) ^ 3)))
    
#define RSP_ADD32(a, b) \
    ((int32_t)((a) + (b)))
    
#define RSP_SUB32(a, b) \
    ((int32_t)((a) - (b)))

#define RSP_SIGNED(val) \
    ((int32_t)(val))

#define SET_DMA_DMEM(dmem_addr) dma_dmem_address = (dmem_addr)
#define SET_DMA_DRAM(dram_addr) dma_dram_address = (dram_addr)
#define DO_DMA_READ(rd_len) dma_rdram_to_dmem(rdram, dma_dmem_address, dma_dram_address, (rd_len))
#define DO_DMA_WRITE(wr_len) dma_dmem_to_rdram(rdram, dma_dmem_address, dma_dram_address, (wr_len))

static inline void dma_rdram_to_dmem(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t rd_len) {
    rd_len += 1; // Read length is inclusive
    dram_addr &= 0xFFFFF8;
    assert(dmem_addr + rd_len <= 0x1000);
    for (uint32_t i = 0; i < rd_len; i++) {
        RSP_MEM_B(i, dmem_addr) = MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000));
    }
}

static inline void dma_dmem_to_rdram(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t wr_len) {
    wr_len += 1; // Write length is inclusive
    dram_addr &= 0xFFFFF8;
    assert(dmem_addr + wr_len <= 0x1000);
    for (uint32_t i = 0; i < wr_len; i++) {
        MEM_B(0, (int64_t)(int32_t)(dram_addr + i + 0x80000000)) = RSP_MEM_B(i, dmem_addr);
    }
}

#endif
