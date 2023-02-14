// This file is modified from the Ares N64 emulator core. Ares can
// be found at https://github.com/ares-emulator/ares. The original license
// for this portion of Ares is as follows:
// ----------------------------------------------------------------------
// ares
// 
// Copyright(c) 2004 - 2021 ares team, Near et al
// 
// Permission to use, copy, modify, and /or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright noticeand this permission notice appear in all copies.
// 
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
// ----------------------------------------------------------------------
#include <cstdint>

#define ARCHITECTURE_AMD64
#define ARCHITECTURE_SUPPORTS_SSE4_1 1

#if defined(ARCHITECTURE_AMD64)
#include <nmmintrin.h>
using v128 = __m128i;
#elif defined(ARCHITECTURE_ARM64)
#include <sse2neon.h>
using v128 = __m128i;
#endif

namespace Accuracy {
    namespace RSP {
#if ARCHITECTURE_SUPPORTS_SSE4_1
        constexpr bool SISD = false;
        constexpr bool SIMD = true;
#else
        constexpr bool SISD = true;
        constexpr bool SIMD = false;
#endif
    }
}

using u8 = uint8_t;
using s8 = int8_t;
using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;
using s32 = int32_t;
using u64 = uint64_t;
using s64 = int64_t;
using uint128_t = uint64_t[2];

template<u32 bits> inline auto sclamp(s64 x) -> s64 {
  enum : s64 { b = 1ull << (bits - 1), m = b - 1 };
  return (x > m) ? m : (x < -b) ? -b : x;
}

struct RSP {
    using r32 = uint32_t;
    using cr32 = const r32;

    union r128 {
        struct { uint64_t u128[2]; };
#if ARCHITECTURE_SUPPORTS_SSE4_1
        struct {   __m128i v128; };

        operator __m128i() const { return v128; }
        auto operator=(__m128i value) { v128 = value; }
#endif

        auto byte(u32 index) -> uint8_t& { return ((uint8_t*)&u128)[15 - index]; }
        auto byte(u32 index) const -> uint8_t { return ((uint8_t*)&u128)[15 - index]; }

        auto element(u32 index) -> uint16_t& { return ((uint16_t*)&u128)[7 - index]; }
        auto element(u32 index) const -> uint16_t { return ((uint16_t*)&u128)[7 - index]; }

        auto u8(u32 index) -> uint8_t& { return ((uint8_t*)&u128)[15 - index]; }
        auto u8(u32 index) const -> uint8_t { return ((uint8_t*)&u128)[15 - index]; }

        auto s16(u32 index) -> int16_t& { return ((int16_t*)&u128)[7 - index]; }
        auto s16(u32 index) const -> int16_t { return ((int16_t*)&u128)[7 - index]; }

        auto u16(u32 index) -> uint16_t& { return ((uint16_t*)&u128)[7 - index]; }
        auto u16(u32 index) const -> uint16_t { return ((uint16_t*)&u128)[7 - index]; }

        //VCx registers
        auto get(u32 index) const -> bool { return u16(index) != 0; }
        auto set(u32 index, bool value) -> bool { return u16(index) = 0 - value, value; }

        //vu-registers.cpp
        auto operator()(u32 index) const -> r128;
    };
    using cr128 = const r128;

    struct VU {
        r128 r[32];
        r128 acch, accm, accl;
        r128 vcoh, vcol;  //16-bit little endian
        r128 vcch, vccl;  //16-bit little endian
        r128 vce;         // 8-bit little endian
        s16 divin;
        s16 divout;
        bool divdp;
    } vpu;

    static constexpr r128 zero{0};
    static constexpr r128 invert{(uint64_t)-1, (uint64_t)-1};

    auto accumulatorGet(u32 index) const -> u64;
    auto accumulatorSet(u32 index, u64 value) -> void;
    auto accumulatorSaturate(u32 index, bool slice, u16 negative, u16 positive) const -> u16;

    auto CFC2(r32& rt, u8 rd) -> void;
    auto CTC2(cr32& rt, u8 rd) -> void;
    template<u8 e> auto LBV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LDV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LFV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LHV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LLV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LPV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LQV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LRV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LSV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LTV(u8 vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LUV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto LWV(r128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto MFC2(r32& rt, cr128& vs) -> void;
    template<u8 e> auto MTC2(cr32& rt, r128& vs) -> void;
    template<u8 e> auto SBV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SDV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SFV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SHV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SLV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SPV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SQV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SRV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SSV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto STV(u8 vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SUV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto SWV(cr128& vt, cr32& rs, s8 imm) -> void;
    template<u8 e> auto VABS(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VADD(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VADDC(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VAND(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VCH(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VCL(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VCR(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VEQ(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VGE(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VLT(r128& vd, cr128& vs, cr128& vt) -> void;
    template<bool U, u8 e>
    auto VMACF(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMACF(r128& vd, cr128& vs, cr128& vt) -> void { VMACF<0, e>(vd, vs, vt); }
    template<u8 e> auto VMACU(r128& vd, cr128& vs, cr128& vt) -> void { VMACF<1, e>(vd, vs, vt); }
    auto VMACQ(r128& vd) -> void;
    template<u8 e> auto VMADH(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMADL(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMADM(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMADN(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMOV(r128& vd, u8 de, cr128& vt) -> void;
    template<u8 e> auto VMRG(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMUDH(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMUDL(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMUDM(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMUDN(r128& vd, cr128& vs, cr128& vt) -> void;
    template<bool U, u8 e>
    auto VMULF(r128& rd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VMULF(r128& rd, cr128& vs, cr128& vt) -> void { VMULF<0, e>(rd, vs, vt); }
    template<u8 e> auto VMULU(r128& rd, cr128& vs, cr128& vt) -> void { VMULF<1, e>(rd, vs, vt); }
    template<u8 e> auto VMULQ(r128& rd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VNAND(r128& rd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VNE(r128& vd, cr128& vs, cr128& vt) -> void;
    auto VNOP() -> void;
    template<u8 e> auto VNOR(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VNXOR(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VOR(r128& vd, cr128& vs, cr128& vt) -> void;
    template<bool L, u8 e>
    auto VRCP(r128& vd, u8 de, cr128& vt) -> void;
    template<u8 e> auto VRCP(r128& vd, u8 de, cr128& vt) -> void { VRCP<0, e>(vd, de, vt); }
    template<u8 e> auto VRCPL(r128& vd, u8 de, cr128& vt) -> void { VRCP<1, e>(vd, de, vt); }
    template<u8 e> auto VRCPH(r128& vd, u8 de, cr128& vt) -> void;
    template<bool D, u8 e>
    auto VRND(r128& vd, u8 vs, cr128& vt) -> void;
    template<u8 e> auto VRNDN(r128& vd, u8 vs, cr128& vt) -> void { VRND<0, e>(vd, vs, vt); }
    template<u8 e> auto VRNDP(r128& vd, u8 vs, cr128& vt) -> void { VRND<1, e>(vd, vs, vt); }
    template<bool L, u8 e>
    auto VRSQ(r128& vd, u8 de, cr128& vt) -> void;
    template<u8 e> auto VRSQ(r128& vd, u8 de, cr128& vt) -> void { VRSQ<0, e>(vd, de, vt); }
    template<u8 e> auto VRSQL(r128& vd, u8 de, cr128& vt) -> void { VRSQ<1, e>(vd, de, vt); }
    template<u8 e> auto VRSQH(r128& vd, u8 de, cr128& vt) -> void;
    template<u8 e> auto VSAR(r128& vd, cr128& vs) -> void;
    template<u8 e> auto VSUB(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VSUBC(r128& vd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VXOR(r128& rd, cr128& vs, cr128& vt) -> void;
    template<u8 e> auto VZERO(r128& rd, cr128& vs, cr128& vt) -> void;
};
