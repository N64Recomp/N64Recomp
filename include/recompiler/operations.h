#ifndef __OPERATIONS_H__
#define __OPERATIONS_H__

#include <unordered_map>

#include "rabbitizer.hpp"

namespace N64Recomp {
    using InstrId = rabbitizer::InstrId::UniqueId;
    using Cop0Reg = rabbitizer::Registers::Cpu::Cop0;

    enum class StoreOpType {
        SD,
        SDL,
        SDR,
        SW,
        SWL,
        SWR,
        SH,
        SB,
        SDC1,
        SWC1
    };

    enum class UnaryOpType {
        None,
        ToS32,
        ToU32,
        ToS64,
        ToU64,
        Lui,
        Mask5, // Mask to 5 bits
        Mask6, // Mask to 5 bits
        ToInt32, // Functionally equivalent to ToS32, only exists for parity with old codegen
        NegateFloat,
        NegateDouble,
        AbsFloat,
        AbsDouble,
        SqrtFloat,
        SqrtDouble,
        ConvertSFromW,
        ConvertWFromS,
        ConvertDFromW,
        ConvertWFromD,
        ConvertDFromS,
        ConvertSFromD,
        ConvertDFromL,
        ConvertLFromD,
        ConvertSFromL,
        ConvertLFromS,
        TruncateWFromS,
        TruncateWFromD,
        TruncateLFromS,
        TruncateLFromD,
        RoundWFromS,
        RoundWFromD,
        RoundLFromS,
        RoundLFromD,
        CeilWFromS,
        CeilWFromD,
        CeilLFromS,
        CeilLFromD,
        FloorWFromS,
        FloorWFromD,
        FloorLFromS,
        FloorLFromD
    };

    enum class BinaryOpType {
        // Addition/subtraction
        Add32,
        Sub32,
        Add64,
        Sub64,
        // Float arithmetic
        AddFloat,
        AddDouble,
        SubFloat,
        SubDouble,
        MulFloat,
        MulDouble,
        DivFloat,
        DivDouble,
        // Bitwise
        And64,
        Or64,
        Nor64,
        Xor64,
        Sll32,
        Sll64,
        Srl32,
        Srl64,
        Sra32,
        Sra64,
        // Comparisons
        Equal,
        NotEqual,
        Less,
        LessEq,
        Greater,
        GreaterEq,
        EqualF32,
        LessF32,
        LessEqF32,
        EqualF64,
        LessF64,
        LessEqF64,
        // Loads
        LD,
        LW,
        LWU,
        LH,
        LHU,
        LB,
        LBU,
        LDL,
        LDR,
        LWL,
        LWR,
        // Fixed result
        True,
        False,

        COUNT,
    };

    enum class Operand {
        Rd, // GPR
        Rs, // GPR
        Rt, // GPR
        Fd, // FPR
        Fs, // FPR
        Ft, // FPR
        FdDouble, // Double float in fd FPR
        FsDouble, // Double float in fs FPR
        FtDouble, // Double float in ft FPR
        // Raw low 32-bit values of FPRs with handling for mips3 float mode behavior
        FdU32L,
        FsU32L,
        FtU32L,
        // Raw high 32-bit values of FPRs with handling for mips3 float mode behavior
        FdU32H,
        FsU32H,
        FtU32H,
        // Raw 64-bit values of FPRs
        FdU64,
        FsU64,
        FtU64,
        ImmU16, // 16-bit immediate, unsigned
        ImmS16, // 16-bit immediate, signed
        Sa, // Shift amount
        Sa32, // Shift amount plus 32
        Cop1cs, // Coprocessor 1 Condition Signal
        Hi,
        Lo,
        Zero,

        Base = Rs, // Alias for Rs for loads
    };

    struct StoreOp {
        StoreOpType type;
        Operand value_input;
    };

    struct UnaryOp {
        UnaryOpType operation;
        Operand output;
        Operand input;
        // Whether the FR bit needs to be checked for odd float registers for this instruction.
        bool check_fr = false;
        // Whether the input need to be checked for being NaN.
        bool check_nan = false;
    };

    struct BinaryOperands {
        // Operation to apply to each operand before applying the binary operation to them.
        UnaryOpType operand_operations[2];
        // The source of the input operands.
        Operand operands[2];
    };

    struct BinaryOp {
        // The type of binary operation this represents.
        BinaryOpType type;
        // The output operand.
        Operand output;
        // The input operands.
        BinaryOperands operands;
        // Whether the FR bit needs to be checked for odd float registers for this instruction.
        bool check_fr = false;
        // Whether the inputs need to be checked for being NaN.
        bool check_nan = false;
    };

    struct ConditionalBranchOp {
        // The type of binary operation to use for this compare
        BinaryOpType comparison;
        // The input operands.
        BinaryOperands operands;
        // Whether this jump should link for returns.
        bool link;
        // Whether this jump has "likely" behavior (doesn't execute the delay slot if skipped).
        bool likely;
    };

    extern const std::unordered_map<InstrId, UnaryOp> unary_ops;
    extern const std::unordered_map<InstrId, BinaryOp> binary_ops;
    extern const std::unordered_map<InstrId, ConditionalBranchOp> conditional_branch_ops;
    extern const std::unordered_map<InstrId, StoreOp> store_ops;
}

#endif
