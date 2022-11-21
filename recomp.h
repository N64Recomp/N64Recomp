#ifndef __RECOMP_H__
#define __RECOMP_H__

#include <stdint.h>
#include <math.h>
#include <assert.h>

#if 0 // treat GPRs as 32-bit, should be better codegen
typedef uint32_t gpr;

#define SIGNED(val) \
    ((int32_t)(val))
#else
typedef uint64_t gpr;

#define SIGNED(val) \
    ((int64_t)(val))
#endif

#define ADD32(a, b) \
    ((gpr)(int32_t)((a) + (b)))

#define SUB32(a, b) \
    ((gpr)(int32_t)((a) - (b)))

#define MEM_W(offset, reg) \
    (*(int32_t*)(rdram + ((((reg) + (offset))) - 0xFFFFFFFF80000000)))
    //(*(int32_t*)(rdram + ((((reg) + (offset))) & 0x3FFFFFF)))

#define MEM_H(offset, reg) \
    (*(int16_t*)(rdram + ((((reg) + (offset)) ^ 2) - 0xFFFFFFFF80000000)))
    //(*(int16_t*)(rdram + ((((reg) + (offset)) ^ 2) & 0x3FFFFFF)))

#define MEM_B(offset, reg) \
    (*(int8_t*)(rdram + ((((reg) + (offset)) ^ 3) - 0xFFFFFFFF80000000)))
    //(*(int8_t*)(rdram + ((((reg) + (offset)) ^ 3) & 0x3FFFFFF)))

#define MEM_HU(offset, reg) \
    (*(uint16_t*)(rdram + ((((reg) + (offset)) ^ 2) - 0xFFFFFFFF80000000)))
    //(*(uint16_t*)(rdram + ((((reg) + (offset)) ^ 2) & 0x3FFFFFF)))

#define MEM_BU(offset, reg) \
    (*(uint8_t*)(rdram + ((((reg) + (offset)) ^ 3) - 0xFFFFFFFF80000000)))
    //(*(uint8_t*)(rdram + ((((reg) + (offset)) ^ 3) & 0x3FFFFFF)))

#define SD(val, offset, reg) { \
    *(uint32_t*)(rdram + ((((reg) + (offset) + 4)) - 0xFFFFFFFF80000000)) = (uint32_t)((val) >> 0); \
    *(uint32_t*)(rdram + ((((reg) + (offset) + 0)) - 0xFFFFFFFF80000000)) = (uint32_t)((val) >> 32); \
}

//#define SD(val, offset, reg) { \
//    *(uint32_t*)(rdram + ((((reg) + (offset) + 4)) & 0x3FFFFFF)) = (uint32_t)((val) >> 32); \
//    *(uint32_t*)(rdram + ((((reg) + (offset) + 0)) & 0x3FFFFFF)) = (uint32_t)((val) >> 0); \
//}

static inline uint64_t load_doubleword(uint8_t* rdram, gpr reg, gpr offset) {
    uint64_t ret = 0;
    uint64_t lo = (uint64_t)(uint32_t)MEM_W(reg, offset + 4);
    uint64_t hi = (uint64_t)(uint32_t)MEM_W(reg, offset + 0);
    ret = (lo << 0) | (hi << 32);
    return ret;
}

#define LD(offset, reg) \
    load_doubleword(rdram, offset, reg)

// TODO proper lwl/lwr/swl/swr
static inline void do_swl(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    uint8_t byte0 = val >> 24;
    uint8_t byte1 = val >> 16;
    uint8_t byte2 = val >> 8;
    uint8_t byte3 = val >> 0;

    MEM_B(offset + 0, reg) = byte0;
    MEM_B(offset + 1, reg) = byte1;
    MEM_B(offset + 2, reg) = byte2;
    MEM_B(offset + 3, reg) = byte3;
}

static inline gpr do_lwl(uint8_t* rdram, gpr offset, gpr reg) {
    uint8_t byte0 = MEM_B(offset + 0, reg);
    uint8_t byte1 = MEM_B(offset + 1, reg);
    uint8_t byte2 = MEM_B(offset + 2, reg);
    uint8_t byte3 = MEM_B(offset + 3, reg);

    // Cast to int32_t to sign extend first
    return (gpr)(int32_t)((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | (byte3 << 0));
}

#define S32(val) \
    ((int32_t)(val))
    
#define U32(val) \
    ((uint32_t)(val))

#define S64(val) \
    ((int64_t)(val))

#define U64(val) \
    ((uint64_t)(val))

#define MUL_S(val1, val2) \
    ((val1) * (val2))

#define MUL_D(val1, val2) \
    ((val1) * (val2))

#define DIV_S(val1, val2) \
    ((val1) / (val2))

#define DIV_D(val1, val2) \
    ((val1) / (val2))

#define CVT_S_W(val) \
    ((float)((int32_t)(val)))

#define CVT_D_W(val) \
    ((double)((int32_t)(val)))

#define CVT_D_S(val) \
    ((double)(val))

#define CVT_S_D(val) \
    ((float)(val))

#define TRUNC_W_S(val) \
    ((int32_t)(val))

#define TRUNC_W_D(val) \
    ((int32_t)(val))

#define NAN_CHECK(val) \
    assert(val == val)

//#define NAN_CHECK(val)

typedef union {
    double d;
    struct {
        float fl;
        float fh;
    };
    struct {
        uint32_t u32l;
        uint32_t u32h;
    };
    uint64_t u64;
} fpr;

typedef struct {
    gpr r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
        r8,  r9,  r10, r11, r12, r13, r14, r15,
        r16, r17, r18, r19, r20, r21, r22, r23,
        r24, r25, r26, r27, r28, r29, r30, r31;
    fpr f0,  f2,  f4,  f6,  f8,  f10, f12, f14,
        f16, f18, f20, f22, f24, f26, f28, f30;
    uint64_t hi, lo;
} recomp_context;

#ifdef __cplusplus
#define restrict __restrict
extern "C" {
#endif

void switch_error(const char* func, uint32_t vram, uint32_t jtbl);
void do_break(uint32_t vram);

typedef void (recomp_func_t)(uint8_t* restrict rdram, recomp_context* restrict ctx);

recomp_func_t* get_function(uint32_t vram);

#define LOOKUP_FUNC(val) \
    get_function(val)

#ifdef __cplusplus
}
#endif

#endif
