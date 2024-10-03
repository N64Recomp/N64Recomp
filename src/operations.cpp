#include "operations.h"

namespace N64Recomp {
    const std::unordered_map<InstrId, UnaryOp> unary_ops {
        { InstrId::cpu_lui,  { UnaryOpType::Lui,  Operand::Rt, Operand::ImmU16 } },
        { InstrId::cpu_mthi, { UnaryOpType::None, Operand::Hi, Operand::Rs } },
        { InstrId::cpu_mtlo, { UnaryOpType::None, Operand::Lo, Operand::Rs } },
        { InstrId::cpu_mfhi, { UnaryOpType::None, Operand::Rd, Operand::Hi } },
        { InstrId::cpu_mflo, { UnaryOpType::None, Operand::Rd, Operand::Lo } },
        { InstrId::cpu_mtc1, { UnaryOpType::None, Operand::FsU32L, Operand::Rt } },
        { InstrId::cpu_mfc1, { UnaryOpType::ToInt32, Operand::Rt, Operand::FsU32L } },
        // Float operations
        { InstrId::cpu_mov_s,     { UnaryOpType::None,           Operand::Fd,       Operand::Fs,       true } },
        { InstrId::cpu_mov_d,     { UnaryOpType::None,           Operand::FdDouble, Operand::FsDouble, true } },
        { InstrId::cpu_neg_s,     { UnaryOpType::NegateFloat,    Operand::Fd,       Operand::Fs,       true, true } },
        { InstrId::cpu_neg_d,     { UnaryOpType::NegateDouble,   Operand::FdDouble, Operand::FsDouble, true, true } },
        { InstrId::cpu_abs_s,     { UnaryOpType::AbsFloat,       Operand::Fd,       Operand::Fs,       true, true } },
        { InstrId::cpu_abs_d,     { UnaryOpType::AbsDouble,      Operand::FdDouble, Operand::FsDouble, true, true } },
        { InstrId::cpu_sqrt_s,    { UnaryOpType::SqrtFloat,      Operand::Fd,       Operand::Fs,       true, true } },
        { InstrId::cpu_sqrt_d,    { UnaryOpType::SqrtDouble,     Operand::FdDouble, Operand::FsDouble, true, true } },
        { InstrId::cpu_cvt_s_w,   { UnaryOpType::ConvertSFromW,  Operand::Fd,       Operand::FsU32L,   true } },
        { InstrId::cpu_cvt_w_s,   { UnaryOpType::ConvertWFromS,  Operand::FdU32L,   Operand::Fs,       true } },
        { InstrId::cpu_cvt_d_w,   { UnaryOpType::ConvertDFromW,  Operand::FdDouble, Operand::FsU32L,   true } },
        { InstrId::cpu_cvt_w_d,   { UnaryOpType::ConvertWFromD,  Operand::FdU32L,   Operand::FsDouble, true } },
        { InstrId::cpu_cvt_d_s,   { UnaryOpType::ConvertDFromS,  Operand::FdDouble, Operand::Fs,       true, true } },
        { InstrId::cpu_cvt_s_d,   { UnaryOpType::ConvertSFromD,  Operand::Fd,       Operand::FsDouble, true, true } },
        { InstrId::cpu_cvt_d_l,   { UnaryOpType::ConvertDFromL,  Operand::FdDouble, Operand::FsU64,    true } },
        { InstrId::cpu_cvt_l_d,   { UnaryOpType::ConvertLFromD,  Operand::FdU64,    Operand::FsDouble, true, true } },
        { InstrId::cpu_cvt_s_l,   { UnaryOpType::ConvertSFromL,  Operand::Fd,       Operand::FsU64,    true } },
        { InstrId::cpu_cvt_l_s,   { UnaryOpType::ConvertLFromS,  Operand::FdU64,    Operand::Fs,       true, true } },
        { InstrId::cpu_trunc_w_s, { UnaryOpType::TruncateWFromS, Operand::FdU32L,   Operand::Fs,       true } },
        { InstrId::cpu_trunc_w_d, { UnaryOpType::TruncateWFromD, Operand::FdU32L,   Operand::FsDouble, true } },
        { InstrId::cpu_round_w_s, { UnaryOpType::RoundWFromS,    Operand::FdU32L,   Operand::Fs,       true } },
        { InstrId::cpu_round_w_d, { UnaryOpType::RoundWFromD,    Operand::FdU32L,   Operand::FsDouble, true } },
        { InstrId::cpu_ceil_w_s,  { UnaryOpType::CeilWFromS,     Operand::FdU32L,   Operand::Fs,       true } },
        { InstrId::cpu_ceil_w_d,  { UnaryOpType::CeilWFromD,     Operand::FdU32L,   Operand::FsDouble, true } },
        { InstrId::cpu_floor_w_s, { UnaryOpType::FloorWFromS,    Operand::FdU32L,   Operand::Fs,       true } },
        { InstrId::cpu_floor_w_d, { UnaryOpType::FloorWFromD,    Operand::FdU32L,   Operand::FsDouble, true } },
    };

    // TODO fix usage of check_nan
    const std::unordered_map<InstrId, BinaryOp> binary_ops {
        // Addition/subtraction
        { InstrId::cpu_addu,   { BinaryOpType::Add32, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_add,    { BinaryOpType::Add32, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_negu,   { BinaryOpType::Sub32, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} }, // pseudo op for subu
        { InstrId::cpu_subu,   { BinaryOpType::Sub32, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_sub,    { BinaryOpType::Sub32, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_daddu,  { BinaryOpType::Add64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_dadd,   { BinaryOpType::Add64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_dsubu,  { BinaryOpType::Sub64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_dsub,   { BinaryOpType::Sub64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::Rt }}} },
        // Addition/subtraction (immediate)
        { InstrId::cpu_addi,   { BinaryOpType::Add32, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        { InstrId::cpu_addiu,  { BinaryOpType::Add32, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        { InstrId::cpu_daddi,  { BinaryOpType::Add64, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        { InstrId::cpu_daddiu, { BinaryOpType::Add64, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        // Bitwise
        { InstrId::cpu_and,    { BinaryOpType::And64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None },  { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_or,     { BinaryOpType::Or64,  Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None },  { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_nor,    { BinaryOpType::Nor64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None },  { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_xor,    { BinaryOpType::Xor64, Operand::Rd, {{ UnaryOpType::None, UnaryOpType::None },  { Operand::Rs, Operand::Rt }}} },
        // Bitwise (immediate)
        { InstrId::cpu_andi,   { BinaryOpType::And64, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmU16 }}} },
        { InstrId::cpu_ori,    { BinaryOpType::Or64,  Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmU16 }}} },
        { InstrId::cpu_xori,   { BinaryOpType::Xor64, Operand::Rt, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Rs, Operand::ImmU16 }}} },
        // Shifts
        /* BUG Should mask after (change op to Sll32 and input op to ToU32) */
        { InstrId::cpu_sllv,   { BinaryOpType::Sll64, Operand::Rd, {{ UnaryOpType::ToS32, UnaryOpType::Mask5 }, { Operand::Rt, Operand::Rs }}} },
        { InstrId::cpu_dsllv,  { BinaryOpType::Sll64, Operand::Rd, {{ UnaryOpType::None,  UnaryOpType::Mask6 }, { Operand::Rt, Operand::Rs }}} },
        { InstrId::cpu_srlv,   { BinaryOpType::Srl32, Operand::Rd, {{ UnaryOpType::ToU32, UnaryOpType::Mask5 }, { Operand::Rt, Operand::Rs }}} },
        { InstrId::cpu_dsrlv,  { BinaryOpType::Srl64, Operand::Rd, {{ UnaryOpType::ToU64, UnaryOpType::Mask6 }, { Operand::Rt, Operand::Rs }}} },
        /* BUG Should mask after (change op to Sra32 and input op to ToS64) */
        { InstrId::cpu_srav,   { BinaryOpType::Sra64, Operand::Rd, {{ UnaryOpType::ToS32, UnaryOpType::Mask5 }, { Operand::Rt, Operand::Rs }}} },
        { InstrId::cpu_dsrav,  { BinaryOpType::Sra64, Operand::Rd, {{ UnaryOpType::ToS64, UnaryOpType::Mask6 }, { Operand::Rt, Operand::Rs }}} },
        // Shifts (immediate)
        /* BUG Should mask after (change op to Sll32 and input op to ToU32) */
        { InstrId::cpu_sll,    { BinaryOpType::Sll64, Operand::Rd, {{ UnaryOpType::ToS32, UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsll,   { BinaryOpType::Sll64, Operand::Rd, {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsll32, { BinaryOpType::Sll64, Operand::Rd, {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rt, Operand::Sa32 }}} },
        { InstrId::cpu_srl,    { BinaryOpType::Srl32, Operand::Rd, {{ UnaryOpType::ToU32, UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsrl,   { BinaryOpType::Srl64, Operand::Rd, {{ UnaryOpType::ToU64, UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsrl32, { BinaryOpType::Srl64, Operand::Rd, {{ UnaryOpType::ToU64, UnaryOpType::None }, { Operand::Rt, Operand::Sa32 }}} },
        /* BUG should cast after (change op to Sra32 and input op to ToS64) */
        { InstrId::cpu_sra,    { BinaryOpType::Sra64, Operand::Rd, {{ UnaryOpType::ToS32, UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsra,   { BinaryOpType::Sra64, Operand::Rd, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rt, Operand::Sa }}} },
        { InstrId::cpu_dsra32, { BinaryOpType::Sra64, Operand::Rd, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rt, Operand::Sa32 }}} },
        // Comparisons
        { InstrId::cpu_slt,   { BinaryOpType::Less, Operand::Rd, {{ UnaryOpType::ToS64, UnaryOpType::ToS64 }, { Operand::Rs, Operand::Rt }}} },
        { InstrId::cpu_sltu,  { BinaryOpType::Less, Operand::Rd, {{ UnaryOpType::ToU64, UnaryOpType::ToU64 }, { Operand::Rs, Operand::Rt }}} },
        // Comparisons (immediate)
        { InstrId::cpu_slti,  { BinaryOpType::Less, Operand::Rt, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        { InstrId::cpu_sltiu, { BinaryOpType::Less, Operand::Rt, {{ UnaryOpType::ToU64, UnaryOpType::None }, { Operand::Rs, Operand::ImmS16 }}} },
        // Float arithmetic
        { InstrId::cpu_add_s, { BinaryOpType::AddFloat,  Operand::Fd,       {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true, true } },
        { InstrId::cpu_add_d, { BinaryOpType::AddDouble, Operand::FdDouble, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true, true } },
        { InstrId::cpu_sub_s, { BinaryOpType::SubFloat,  Operand::Fd,       {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true, true } },
        { InstrId::cpu_sub_d, { BinaryOpType::SubDouble, Operand::FdDouble, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true, true } },
        { InstrId::cpu_mul_s, { BinaryOpType::MulFloat,  Operand::Fd,       {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true, true } },
        { InstrId::cpu_mul_d, { BinaryOpType::MulDouble, Operand::FdDouble, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true, true } },
        { InstrId::cpu_div_s, { BinaryOpType::DivFloat,  Operand::Fd,       {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true, true } },
        { InstrId::cpu_div_d, { BinaryOpType::DivDouble, Operand::FdDouble, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true, true } },
        // Float comparisons TODO remaining operations and investigate ordered/unordered and default values
        { InstrId::cpu_c_lt_s,  { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_nge_s, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_olt_s, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ult_s, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_lt_d,  { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_nge_d, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_olt_d, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ult_d, { BinaryOpType::Less,   Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },

        { InstrId::cpu_c_le_s,  { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ngt_s, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ole_s, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ule_s, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_le_d,  { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ngt_d, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ole_d, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ule_d, { BinaryOpType::LessEq, Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },

        { InstrId::cpu_c_eq_s,  { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ueq_s, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_ngl_s, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_seq_s, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::Fs, Operand::Ft }}, true } },
        { InstrId::cpu_c_eq_d,  { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ueq_d, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        { InstrId::cpu_c_ngl_d, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        /* TODO rename to c_seq_d when fixed in rabbitizer */
        { InstrId::cpu_c_deq_d, { BinaryOpType::Equal,  Operand::Cop1cs, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::FsDouble, Operand::FtDouble }}, true } },
        // Loads
        { InstrId::cpu_ld,   { BinaryOpType::LD,  Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lw,   { BinaryOpType::LW,  Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lwu,  { BinaryOpType::LWU, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lh,   { BinaryOpType::LH,  Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lhu,  { BinaryOpType::LHU, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lb,   { BinaryOpType::LB,  Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lbu,  { BinaryOpType::LBU, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_ldl,  { BinaryOpType::LDL, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_ldr,  { BinaryOpType::LDR, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lwl,  { BinaryOpType::LWL, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lwr,  { BinaryOpType::LWR, Operand::Rt,    {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_lwc1, { BinaryOpType::LW, Operand::FtU32L, {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
        { InstrId::cpu_ldc1, { BinaryOpType::LD, Operand::FtU64,  {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}, true } },
    };

    const std::unordered_map<InstrId, ConditionalBranchOp> conditional_branch_ops {
        { InstrId::cpu_beq,     { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rs, Operand::Rt }},   false, false }},
        { InstrId::cpu_beql,    { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rs, Operand::Rt }},   false, true }},
        { InstrId::cpu_bne,     { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rs, Operand::Rt }},   false, false }},
        { InstrId::cpu_bnel,    { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Rs, Operand::Rt }},   false, true }},
        { InstrId::cpu_bgez,    { BinaryOpType::GreaterEq, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, false }},
        { InstrId::cpu_bgezl,   { BinaryOpType::GreaterEq, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, true }},
        { InstrId::cpu_bgtz,    { BinaryOpType::Greater,   {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, false }},
        { InstrId::cpu_bgtzl,   { BinaryOpType::Greater,   {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, true }},
        { InstrId::cpu_blez,    { BinaryOpType::LessEq,    {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, false }},
        { InstrId::cpu_blezl,   { BinaryOpType::LessEq,    {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, true }},
        { InstrId::cpu_bltz,    { BinaryOpType::Less,      {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, false }},
        { InstrId::cpu_bltzl,   { BinaryOpType::Less,      {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, false, true }},
        { InstrId::cpu_bgezal,  { BinaryOpType::GreaterEq, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, true, false }},
        { InstrId::cpu_bgezall, { BinaryOpType::GreaterEq, {{ UnaryOpType::ToS64, UnaryOpType::None }, { Operand::Rs, Operand::Zero }}, true, true }},
        { InstrId::cpu_bc1f,    { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, false }},
        { InstrId::cpu_bc1fl,   { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, true }},
        { InstrId::cpu_bc1t,    { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, false }},
        { InstrId::cpu_bc1tl,   { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, true }},
    };

    const std::unordered_map<InstrId, StoreOp> store_ops {
        { InstrId::cpu_sd,   { StoreOpType::SD,   Operand::Rt }},
        { InstrId::cpu_sdl,  { StoreOpType::SDL,  Operand::Rt }},
        { InstrId::cpu_sdr,  { StoreOpType::SDR,  Operand::Rt }},
        { InstrId::cpu_sw,   { StoreOpType::SW,   Operand::Rt }},
        { InstrId::cpu_swl,  { StoreOpType::SWL,  Operand::Rt }},
        { InstrId::cpu_swr,  { StoreOpType::SWR,  Operand::Rt }},
        { InstrId::cpu_sh,   { StoreOpType::SH,   Operand::Rt }},
        { InstrId::cpu_sb,   { StoreOpType::SB,   Operand::Rt }},
        { InstrId::cpu_sdc1, { StoreOpType::SDC1, Operand::FtU64 }},
        { InstrId::cpu_swc1, { StoreOpType::SWC1, Operand::FtU32L }},
    };
}