#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recomp_port.h"

using InstrId = rabbitizer::InstrId::UniqueId;
using Cop0Reg = rabbitizer::Registers::Cpu::Cop0;

std::string_view ctx_gpr_prefix(int reg) {
    if (reg != 0) {
        return "ctx->r";
    }
    return "";
}

enum class UnaryOpType {
    None,
    ToS32,
    ToU32,
    ToS64,
    ToU64,
    NegateS32,
    NegateS64,
    Lui,
    Mask5, // Mask to 5 bits
    Mask6, // Mask to 5 bits
    ToInt32, // Functionally equivalent to ToS32, only exists for parity with old codegen
};

enum class BinaryOpType {
    // Addition/subtraction
    Add32,
    Sub32,
    Add64,
    Sub64,
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

struct UnaryOp {
    UnaryOpType operation;
    Operand output;
    Operand input;
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

const std::unordered_map<InstrId, UnaryOp> unary_ops {
    { InstrId::cpu_lui,  { UnaryOpType::Lui,  Operand::Rt, Operand::ImmU16 } },
    { InstrId::cpu_mthi, { UnaryOpType::None, Operand::Hi, Operand::Rs } },
    { InstrId::cpu_mtlo, { UnaryOpType::None, Operand::Lo, Operand::Rs } },
    { InstrId::cpu_mfhi, { UnaryOpType::None, Operand::Rd, Operand::Hi } },
    { InstrId::cpu_mflo, { UnaryOpType::None, Operand::Rd, Operand::Lo } },
    { InstrId::cpu_mtc1, { UnaryOpType::None, Operand::FsU32L, Operand::Rt } },
    { InstrId::cpu_mfc1, { UnaryOpType::ToInt32, Operand::Rt, Operand::FsU32L } },
};

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
    { InstrId::cpu_ldc1, { BinaryOpType::LD, Operand::FtU64,  {{ UnaryOpType::None, UnaryOpType::None }, { Operand::ImmS16, Operand::Base }}} },
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
    { InstrId::cpu_bc1f,    { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, false }},
    { InstrId::cpu_bc1fl,   { BinaryOpType::NotEqual,  {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, true }},
    { InstrId::cpu_bc1t,    { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, false }},
    { InstrId::cpu_bc1tl,   { BinaryOpType::Equal,     {{ UnaryOpType::None,  UnaryOpType::None }, { Operand::Cop1cs, Operand::Zero }}, false, true }},
};

struct InstructionContext {
    int rd;
    int rs;
    int rt;
    int sa;

    int fd;
    int fs;
    int ft;

    int cop1_cs;

    uint16_t imm16;

    RecompPort::RelocType reloc_type;
    uint32_t reloc_section_index;
    uint32_t reloc_target_section_offset;
};

class CGenerator {
public:
    CGenerator() = default;
    void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx);
    void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx);
    void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx);
    void emit_branch_close(std::ostream& output_file);
private:
    void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string);
    void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string);
    void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string);
};

struct BinaryOpFields { std::string func_string; std::string infix_string; };

std::vector<BinaryOpFields> c_op_fields = []() {
    std::vector<BinaryOpFields> ret{};
    ret.resize(static_cast<size_t>(BinaryOpType::COUNT));
    std::vector<char> ops_setup{};
    ops_setup.resize(static_cast<size_t>(BinaryOpType::COUNT));

    auto setup_op = [&ret, &ops_setup](BinaryOpType op_type, const std::string& func_string, const std::string& infix_string) {
        size_t index = static_cast<size_t>(op_type);
        // Prevent setting up an operation twice.
        assert(ops_setup[index] == false && "Operation already setup!");
        ops_setup[index] = true;
        ret[index] = { func_string, infix_string };
    };

    setup_op(BinaryOpType::Add32,     "ADD32",  "");
    setup_op(BinaryOpType::Sub32,     "SUB32",  "");
    setup_op(BinaryOpType::Add64,     "",       "+");
    setup_op(BinaryOpType::Sub64,     "",       "-");
    setup_op(BinaryOpType::And64,     "",       "&");
    setup_op(BinaryOpType::Or64,      "",       "|");
    setup_op(BinaryOpType::Nor64,     "~",      "|");
    setup_op(BinaryOpType::Xor64,     "",       "^");
    setup_op(BinaryOpType::Sll32,     "S32",    "<<");
    setup_op(BinaryOpType::Sll64,     "",       "<<");
    setup_op(BinaryOpType::Srl32,     "S32",    ">>");
    setup_op(BinaryOpType::Srl64,     "",       ">>");
    setup_op(BinaryOpType::Sra32,     "S32",    ">>"); // Arithmetic aspect will be taken care of by unary op for first operand.
    setup_op(BinaryOpType::Sra64,     "",       ">>"); // Arithmetic aspect will be taken care of by unary op for first operand.
    setup_op(BinaryOpType::Equal,     "",       "==");
    setup_op(BinaryOpType::NotEqual,  "",       "!=");
    setup_op(BinaryOpType::Less,      "",       "<");
    setup_op(BinaryOpType::LessEq,    "",       "<=");
    setup_op(BinaryOpType::Greater,   "",       ">");
    setup_op(BinaryOpType::GreaterEq, "",       ">=");
    setup_op(BinaryOpType::LD,        "LD",     "");
    setup_op(BinaryOpType::LW,        "MEM_W",  "");
    setup_op(BinaryOpType::LWU,       "MEM_WU", "");
    setup_op(BinaryOpType::LH,        "MEM_H",  "");
    setup_op(BinaryOpType::LHU,       "MEM_HU", "");
    setup_op(BinaryOpType::LB,        "MEM_B",  "");
    setup_op(BinaryOpType::LBU,       "MEM_BU", "");
    setup_op(BinaryOpType::LDL,       "do_ldl", "");
    setup_op(BinaryOpType::LDR,       "do_ldr", "");
    setup_op(BinaryOpType::LWL,       "do_lwl", "");
    setup_op(BinaryOpType::LWR,       "do_lwr", "");
    setup_op(BinaryOpType::True,      "", "");
    setup_op(BinaryOpType::False,     "", "");

    // Ensure every operation has been setup.
    for (char is_set : ops_setup) {
        assert(is_set && "Operation has not been setup!");
    }

    return ret;
}();

std::string gpr_to_string(int gpr_index) {
    if (gpr_index == 0) {
        return "0";
    }
    return fmt::format("ctx->r{}", gpr_index);
}

std::string fpr_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.fl", fpr_index);
}

std::string fpr_double_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.d", fpr_index);
}

std::string fpr_u32l_to_string(int fpr_index) {
    if (fpr_index & 1) {
        return fmt::format("ctx->f_odd[({} - 1) * 2]", fpr_index);
    }
    else {
        return fmt::format("ctx->f{}.u32l", fpr_index);
    }
}

std::string fpr_u64_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.u64", fpr_index);
}

std::string unsigned_reloc(const InstructionContext& context) {
    switch (context.reloc_type) {
        case RecompPort::RelocType::R_MIPS_HI16:
            return fmt::format("RELOC_HI16({}, {:#X})", context.reloc_section_index, context.reloc_target_section_offset);
        case RecompPort::RelocType::R_MIPS_LO16:
            return fmt::format("RELOC_LO16({}, {:#X})", context.reloc_section_index, context.reloc_target_section_offset);
        default:
            throw std::runtime_error(fmt::format("Unexpected reloc type {}\n", static_cast<int>(context.reloc_type)));
    }
}

std::string signed_reloc(const InstructionContext& context) {
    return "(int16_t)" + unsigned_reloc(context);
}

void CGenerator::get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) {
    switch (operand) {
        case Operand::Rd:
            operand_string = gpr_to_string(context.rd);
            break;
        case Operand::Rs:
            operand_string = gpr_to_string(context.rs);
            break;
        case Operand::Rt:
            operand_string = gpr_to_string(context.rt);
            break;
        case Operand::Fd:
            operand_string = fpr_to_string(context.fd);
            break;
        case Operand::Fs:
            operand_string = fpr_to_string(context.fs);
            break;
        case Operand::Ft:
            operand_string = fpr_to_string(context.ft);
            break;
        case Operand::FdDouble:
            operand_string = fpr_double_to_string(context.fd);
            break;
        case Operand::FsDouble:
            operand_string = fpr_double_to_string(context.fs);
            break;
        case Operand::FtDouble:
            operand_string = fpr_double_to_string(context.ft);
            break;
        case Operand::FdU32L:
            operand_string = fpr_u32l_to_string(context.fd);
            break;
        case Operand::FsU32L:
            operand_string = fpr_u32l_to_string(context.fs);
            break;
        case Operand::FtU32L:
            operand_string = fpr_u32l_to_string(context.ft);
            break;
        case Operand::FdU32H:
            assert(false);
            break;
        case Operand::FsU32H:
            assert(false);
            break;
        case Operand::FtU32H:
            assert(false);
            break;
        case Operand::FdU64:
            operand_string = fpr_u64_to_string(context.fd);
            break;
        case Operand::FsU64:
            operand_string = fpr_u64_to_string(context.fs);
            break;
        case Operand::FtU64:
            operand_string = fpr_u64_to_string(context.ft);
            break;
        case Operand::ImmU16:
            if (context.reloc_type != RecompPort::RelocType::R_MIPS_NONE) {
                operand_string = unsigned_reloc(context);
            }
            else {
                operand_string = fmt::format("{:#X}", context.imm16);
            }
            break;
        case Operand::ImmS16:
            if (context.reloc_type != RecompPort::RelocType::R_MIPS_NONE) {
                operand_string = signed_reloc(context);
            }
            else {
                operand_string = fmt::format("{:#X}", (int16_t)context.imm16);
            }
            break;
        case Operand::Sa:
            operand_string = std::to_string(context.sa);
            break;
        case Operand::Sa32:
            operand_string = fmt::format("({} + 32)", context.sa);
            break;
        case Operand::Cop1cs:
            operand_string = fmt::format("c1cs");
            break;
        case Operand::Hi:
            operand_string = "hi";
            break;
        case Operand::Lo:
            operand_string = "lo";
            break;
        case Operand::Zero:
            operand_string = "0";
            break;
    }
    switch (operation) {
        case UnaryOpType::None:
            break;
        case UnaryOpType::ToS32:
            operand_string = "S32(" + operand_string + ")"; 
            break;
        case UnaryOpType::ToU32:
            operand_string = "U32(" + operand_string + ")"; 
            break;
        case UnaryOpType::ToS64:
            operand_string = "SIGNED(" + operand_string + ")"; 
            break;
        case UnaryOpType::ToU64:
            // Nothing to do here, they're already U64
            break;
        case UnaryOpType::NegateS32:
            assert(false);
            break;
        case UnaryOpType::NegateS64:
            assert(false);
            break;
        case UnaryOpType::Lui:
            operand_string = "S32(" + operand_string + " << 16)"; 
            break;
        case UnaryOpType::Mask5:
            operand_string = "(" + operand_string + " & 31)";
            break;
        case UnaryOpType::Mask6:
            operand_string = "(" + operand_string + " & 63)";
            break;
        case UnaryOpType::ToInt32:
            operand_string = "(int32_t)" + operand_string; 
            break;
    }
}

void CGenerator::get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) {
    func_string = c_op_fields[static_cast<size_t>(op_type)].func_string;
    infix_string = c_op_fields[static_cast<size_t>(op_type)].infix_string;
}

void CGenerator::get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) {
    thread_local std::string input_a{};
    thread_local std::string input_b{};
    thread_local std::string func_string{};
    thread_local std::string infix_string{};
    bool is_infix;
    get_operand_string(operands.operands[0], operands.operand_operations[0], ctx, input_a);
    get_operand_string(operands.operands[1], operands.operand_operations[1], ctx, input_b);
    get_notation(type, func_string, infix_string);
    
    // These cases aren't strictly necessary and are just here for parity with the old recompiler output.
    if (type == BinaryOpType::Less && !(operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None)) {
        expr_string = fmt::format("{} {} {} ? 1 : 0", input_a, infix_string, input_b);
    }
    else if (type == BinaryOpType::Equal && operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) {
        expr_string = input_a;
    }
    else if (type == BinaryOpType::NotEqual && operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) {
        expr_string = "!" + input_a;
    }
    // End unnecessary cases.

    // TODO encode these ops to avoid needing special handling.
    else if (type == BinaryOpType::LWL || type == BinaryOpType::LWR || type == BinaryOpType::LDL || type == BinaryOpType::LDR) {
        expr_string = fmt::format("{}(rdram, {}, {}, {})", func_string, output, input_a, input_b);
    }
    else if (!func_string.empty() && !infix_string.empty()) {
        expr_string = fmt::format("{}({} {} {})", func_string, input_a, infix_string, input_b);
    }
    else if (!func_string.empty()) {
        expr_string = fmt::format("{}({}, {})", func_string, input_a, input_b);
    }
    else if (!infix_string.empty()) {
        expr_string = fmt::format("{} {} {}", input_a, infix_string, input_b);
    }
    else {
        // Handle special cases
        if (type == BinaryOpType::True) {
            expr_string = "1";
        }
        else if (type == BinaryOpType::False) {
            expr_string = "0";
        }
        assert(false && "Binary operation must have either a function or infix!");
    }
}

void CGenerator::emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string expr_string{};
    get_binary_expr_string(op.comparison, op.operands, ctx, "", expr_string);
    fmt::print(output_file, "if ({}) {{\n", expr_string);
}

void CGenerator::emit_branch_close(std::ostream& output_file) {
    fmt::print(output_file, "    }}\n");
}

void CGenerator::process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string expression{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_binary_expr_string(op.type, op.operands, ctx, output, expression);

    // TODO remove this after the refactor is done, temporary hack to match the old recompiler output.
    if (op.type == BinaryOpType::LD && op.output == Operand::FtU64) {
        fmt::print(output_file, "CHECK_FR(ctx, {});\n    ", ctx.ft);
    }

    fmt::print(output_file, "{} = {};\n", output, expression);
}

void CGenerator::process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string input{};
    bool is_infix;
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_operand_string(op.input, op.operation, ctx, input);
    fmt::print(output_file, "{} = {};\n", output, input);
}

// Major TODO, this function grew very organically and needs to be cleaned up. Ideally, it'll get split up into some sort of lookup table grouped by similar instruction types.
bool process_instruction(const RecompPort::Context& context, const RecompPort::Config& config, const RecompPort::Function& func, const RecompPort::FunctionStats& stats, const std::unordered_set<uint32_t>& skipped_insns, size_t instr_index, const std::vector<rabbitizer::InstructionCpu>& instructions, std::ofstream& output_file, bool indent, bool emit_link_branch, int link_branch_index, size_t reloc_index, bool& needs_link_branch, bool& is_branch_likely, std::span<std::vector<uint32_t>> static_funcs_out) {
    const auto& section = context.sections[func.section_index];
    const auto& instr = instructions[instr_index];
    needs_link_branch = false;
    is_branch_likely = false;
    uint32_t instr_vram = instr.getVram();

    auto print_indent = [&]() {
        fmt::print(output_file, "    ");
    };

    auto hook_find = func.function_hooks.find(instr_index);
    if (hook_find != func.function_hooks.end()) {
        fmt::print(output_file, "    {}\n", hook_find->second);
        if (indent) {
            print_indent();
        }
    }

    // Output a comment with the original instruction
    if (instr.isBranch() || instr.getUniqueId() == InstrId::cpu_j) {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0, fmt::format("L_{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else if (instr.getUniqueId() == InstrId::cpu_jal) {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0, fmt::format("0x{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0));
    }

    if (skipped_insns.contains(instr_vram)) {
        return true;
    }

    bool at_reloc = false;
    bool reloc_handled = false;
    RecompPort::RelocType reloc_type = RecompPort::RelocType::R_MIPS_NONE;
    uint32_t reloc_section = 0;
    uint32_t reloc_target_section_offset = 0;
    size_t reloc_reference_symbol = (size_t)-1;

    uint32_t func_vram_end = func.vram + func.words.size() * sizeof(func.words[0]);

    uint16_t imm = instr.Get_immediate();

    // Check if this instruction has a reloc.
    if (section.relocs.size() > 0 && section.relocs[reloc_index].address == instr_vram) {
        // Get the reloc data for this instruction
        const auto& reloc = section.relocs[reloc_index];
        reloc_section = reloc.target_section;
        // Only process this relocation if this section is relocatable or if this relocation targets a reference symbol.
        if (section.relocatable || reloc.reference_symbol) {
            // Some symbols are in a nonexistent section (e.g. absolute symbols), so check that the section is valid before doing anything else.
            // Absolute symbols will never need to be relocated so it's safe to skip this.
            // Always process reference symbols relocations.
            if (reloc_section < context.sections.size() || reloc.reference_symbol) {
                // Ignore this reloc if it points to a different section.
                // Also check if the reloc points to the bss section since that will also be relocated with the section.
                // Additionally, always process reference symbol relocations.
                if (reloc_section == func.section_index || reloc_section == section.bss_section_index || reloc.reference_symbol) {
                    // Record the reloc's data.
                    reloc_type = reloc.type;
                    reloc_target_section_offset = reloc.section_offset;
                    // Ignore all relocs that aren't HI16 or LO16.
                    if (reloc_type == RecompPort::RelocType::R_MIPS_HI16 || reloc_type == RecompPort::RelocType::R_MIPS_LO16 || reloc_type == RecompPort::RelocType::R_MIPS_26) {
                        at_reloc = true;

                        if (reloc.reference_symbol) {
                            reloc_reference_symbol = reloc.symbol_index;
                            static RecompPort::ReferenceSection dummy_section{
                                .rom_addr = 0,
                                .ram_addr = 0,
                                .size = 0,
                                .relocatable = false
                            };
                            const auto& reloc_reference_section = reloc.target_section == RecompPort::SectionAbsolute ? dummy_section : context.reference_sections[reloc.target_section];
                            if (!reloc_reference_section.relocatable) {
                                at_reloc = false;
                                uint32_t full_immediate = reloc.section_offset + reloc_reference_section.ram_addr;
                                
                                if (reloc_type == RecompPort::RelocType::R_MIPS_HI16) {
                                    imm = (full_immediate >> 16) + ((full_immediate >> 15) & 1);
                                }
                                else if (reloc_type == RecompPort::RelocType::R_MIPS_LO16) {
                                    imm = full_immediate & 0xFFFF;
                                }
                            }
                        }
                    }

                    // Repoint bss relocations at their non-bss counterpart section.
                    if (reloc_section == section.bss_section_index) {
                        reloc_section = func.section_index;
                    }
                }
            }
        }
    }

    auto print_line = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts ...args) {
        print_indent();
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        fmt::print(output_file, ";\n");
    };

    auto print_branch_condition = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts ...args) {
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        fmt::print(output_file, " ");
    };

    auto print_unconditional_branch = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts ...args) {
        if (instr_index < instructions.size() - 1) {
            bool dummy_needs_link_branch;
            bool dummy_is_branch_likely;
            size_t next_reloc_index = reloc_index;
            uint32_t next_vram = instr_vram + 4;
            if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                next_reloc_index++;
            }
            process_instruction(context, config, func, stats, skipped_insns, instr_index + 1, instructions, output_file, false, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out);
        }
        print_indent();
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        if (needs_link_branch) {
            fmt::print(output_file, ";\n    goto after_{};\n", link_branch_index);
        } else {
            fmt::print(output_file, ";\n");
        }
    };

    auto print_func_call = [reloc_target_section_offset, reloc_reference_symbol, reloc_type, &context, &section, &func, &static_funcs_out, &needs_link_branch, &print_unconditional_branch]
        (uint32_t target_func_vram, bool link_branch = true, bool indent = false)
    {
        std::string jal_target_name;
        if (reloc_reference_symbol != (size_t)-1) {
            const auto& ref_symbol = context.reference_symbols[reloc_reference_symbol];
            const std::string& ref_symbol_name = context.reference_symbol_names[reloc_reference_symbol];

            if (reloc_type != RecompPort::RelocType::R_MIPS_26) {
                fmt::print(stderr, "Unsupported reloc type {} on jal instruction in {}\n", (int)reloc_type, func.name);
                return false;
            }

            if (ref_symbol.section_offset != reloc_target_section_offset) {
                fmt::print(stderr, "Function {} uses a MIPS_R_26 addend, which is not supported yet\n", func.name);
                return false;
            }

            jal_target_name = ref_symbol_name;
        }
        else {
            const auto matching_funcs_find = context.functions_by_vram.find(target_func_vram);
            uint32_t section_vram_start = section.ram_addr;
            uint32_t section_vram_end = section.ram_addr + section.size;
            // TODO the current section should be prioritized if the target jal is in its vram even if a function isn't known (i.e. static)
            if (matching_funcs_find != context.functions_by_vram.end()) {
                // If we found matches for the target function by vram, 
                const auto& matching_funcs_vec = matching_funcs_find->second;
                size_t real_func_index;
                bool ambiguous;
                // If there is more than one corresponding function, look for any that have a nonzero size.
                if (matching_funcs_vec.size() > 1) {
                    size_t nonzero_func_index = (size_t)-1;
                    bool found_nonzero_func = false;
                    for (size_t cur_func_index : matching_funcs_vec) {
                        const auto& cur_func = context.functions[cur_func_index];
                        if (cur_func.words.size() != 0) {
                            if (found_nonzero_func) {
                                ambiguous = true;
                                break;
                            }
                            // If this section is relocatable and the target vram is in the section, don't call functions
                            // in any section other than this one.
                            if (cur_func.section_index == func.section_index ||
                                !(section.relocatable && target_func_vram >= section_vram_start && target_func_vram < section_vram_end)) {
                                found_nonzero_func = true;
                                nonzero_func_index = cur_func_index;
                            }
                        }
                    }
                    if (nonzero_func_index == (size_t)-1) {
                        fmt::print(stderr, "[Warn] Potential jal resolution ambiguity\n");
                        for (size_t cur_func_index : matching_funcs_vec) {
                            fmt::print(stderr, "  {}\n", context.functions[cur_func_index].name);
                        }
                        nonzero_func_index = 0;
                    }
                    real_func_index = nonzero_func_index;
                    ambiguous = false;
                }
                else {
                    real_func_index = matching_funcs_vec.front();
                    ambiguous = false;
                }
                if (ambiguous) {
                    fmt::print(stderr, "Ambiguous jal target: 0x{:08X}\n", target_func_vram);
                    for (size_t cur_func_index : matching_funcs_vec) {
                        const auto& cur_func = context.functions[cur_func_index];
                        fmt::print(stderr, "  {}\n", cur_func.name);
                    }
                    return false;
                }
                jal_target_name = context.functions[real_func_index].name;
            }
            else {
                const auto& section = context.sections[func.section_index];
                if (target_func_vram >= section.ram_addr && target_func_vram < section.ram_addr + section.size) {
                    jal_target_name = fmt::format("static_{}_{:08X}", func.section_index, target_func_vram);
                    static_funcs_out[func.section_index].push_back(target_func_vram);
                }
                else {
                    fmt::print(stderr, "No function found for jal target: 0x{:08X}\n", target_func_vram);
                    return false;
                }
            }
        }
        needs_link_branch = link_branch;
        if (indent) {
            print_unconditional_branch("    {}(rdram, ctx)", jal_target_name);
        } else {
            print_unconditional_branch("{}(rdram, ctx)", jal_target_name);
        }
        return true;
    };

    auto print_branch = [&](uint32_t branch_target) {
        if (branch_target < func.vram || branch_target >= func_vram_end) {
            // FIXME: how to deal with static functions?
            if (context.functions_by_vram.find(branch_target) != context.functions_by_vram.end()) {
                fmt::print("Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                print_func_call(branch_target, false, true);
                print_line("    return");
                fmt::print(output_file, "    }}\n");
                return;
            }

            fmt::print(stderr, "[Warn] Function {} is branching outside of the function (to 0x{:08X})\n", func.name, branch_target);
        }

        if (instr_index < instructions.size() - 1) {
            bool dummy_needs_link_branch;
            bool dummy_is_branch_likely;
            size_t next_reloc_index = reloc_index;
            uint32_t next_vram = instr_vram + 4;
            if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                next_reloc_index++;
            }
            process_instruction(context, config, func, stats, skipped_insns, instr_index + 1, instructions, output_file, true, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out);
        }

        fmt::print(output_file, "        goto L_{:08X};\n", branch_target);
        if (needs_link_branch) {
            fmt::print(output_file, "        goto after_{};\n", link_branch_index);
        }
    };

    if (indent) {
        print_indent();
    }

    int rd = (int)instr.GetO32_rd();
    int rs = (int)instr.GetO32_rs();
    int base = rs;
    int rt = (int)instr.GetO32_rt();
    int sa = (int)instr.Get_sa();

    int fd = (int)instr.GetO32_fd();
    int fs = (int)instr.GetO32_fs();
    int ft = (int)instr.GetO32_ft();

    int cop1_cs = (int)instr.Get_cop1cs();

    std::string unsigned_imm_string;
    std::string signed_imm_string;

    if (!at_reloc) {
        unsigned_imm_string = fmt::format("{:#X}", imm);
        signed_imm_string = fmt::format("{:#X}", (int16_t)imm);
    } else {
        switch (reloc_type) {
            case RecompPort::RelocType::R_MIPS_HI16:
                unsigned_imm_string = fmt::format("RELOC_HI16({}, {:#X})", reloc_section, reloc_target_section_offset);
                signed_imm_string = "(int16_t)" + unsigned_imm_string;
                reloc_handled = true;
                break;
            case RecompPort::RelocType::R_MIPS_LO16:
                unsigned_imm_string = fmt::format("RELOC_LO16({}, {:#X})", reloc_section, reloc_target_section_offset);
                signed_imm_string = "(int16_t)" + unsigned_imm_string;
                reloc_handled = true;
                break;
            case RecompPort::RelocType::R_MIPS_26:
                // Nothing to do here, this will be handled by print_func_call.
                reloc_handled = true;
                break;
            default:
                throw std::runtime_error(fmt::format("Unexpected reloc type {} in {}\n", static_cast<int>(reloc_type), func.name));
        }
    }

    bool handled = true;

    switch (instr.getUniqueId()) {
    case InstrId::cpu_nop:
        fmt::print(output_file, "\n");
        break;
    // Cop0 (Limited functionality)
    case InstrId::cpu_mfc0:
        {
            Cop0Reg reg = instr.Get_cop0d();
            switch (reg) {
            case Cop0Reg::COP0_Status:
                print_line("{}{} = cop0_status_read(ctx)", ctx_gpr_prefix(rt), rt);
                break;
            default:
                fmt::print(stderr, "Unhandled cop0 register in mfc0: {}\n", (int)reg);
                return false;
            }
            break;
        }
    case InstrId::cpu_mtc0:
        {
            Cop0Reg reg = instr.Get_cop0d();
            switch (reg) {
            case Cop0Reg::COP0_Status:
                print_line("cop0_status_write(ctx, {}{})", ctx_gpr_prefix(rt), rt);
                break;
            default:
                fmt::print(stderr, "Unhandled cop0 register in mtc0: {}\n", (int)reg);
                return false;
            }
            break;
        }
    // Arithmetic
    case InstrId::cpu_add:
    case InstrId::cpu_addu:
        {
            // Check if this addu belongs to a jump table load
            auto find_result = std::find_if(stats.jump_tables.begin(), stats.jump_tables.end(),
                [instr_vram](const RecompPort::JumpTable& jtbl) {
                return jtbl.addu_vram == instr_vram;
            });
            // If so, create a temp to preserve the addend register's value
            if (find_result != stats.jump_tables.end()) {
                const RecompPort::JumpTable& cur_jtbl = *find_result;
                print_line("gpr jr_addend_{:08X} = {}{}", cur_jtbl.jr_vram, ctx_gpr_prefix(cur_jtbl.addend_reg), cur_jtbl.addend_reg);
            }
        }
        break;
    case InstrId::cpu_mult:
        print_line("result = S64(S32({}{})) * S64(S32({}{})); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_dmult:
        print_line("DMULT(S64({}{}), S64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_multu:
        print_line("result = U64(U32({}{})) * U64(U32({}{})); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_dmultu:
        print_line("DMULTU(U64({}{}), U64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_div:
        // Cast to 64-bits before division to prevent artihmetic exception for s32(0x80000000) / -1
        print_line("lo = S32(S64(S32({}{})) / S64(S32({}{}))); hi = S32(S64(S32({}{})) % S64(S32({}{})))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_ddiv:
        print_line("DDIV(S64({}{}), S64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_divu:
        print_line("lo = S32(U32({}{}) / U32({}{})); hi = S32(U32({}{}) % U32({}{}))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_ddivu:
        print_line("DDIVU(U64({}{}), U64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    // Stores
    case InstrId::cpu_sd:
        print_line("SD({}{}, {}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_sw:
        print_line("MEM_W({}, {}{}) = {}{}", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_sh:
        print_line("MEM_H({}, {}{}) = {}{}", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_sb:
        print_line("MEM_B({}, {}{}) = {}{}", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
        break;
    // Unaligned loads
    // examples:
    // reg =        11111111 01234567
    // mem @ x =             89ABCDEF

    // LWL x + 0 -> FFFFFFFF 89ABCDEF
    // LWL x + 1 -> FFFFFFFF ABCDEF67
    // LWL x + 2 -> FFFFFFFF CDEF4567
    // LWL x + 3 -> FFFFFFFF EF234567

    // LWR x + 0 -> 00000000 01234589
    // LWR x + 1 -> 00000000 012389AB
    // LWR x + 2 -> 00000000 0189ABCD
    // LWR x + 3 -> FFFFFFFF 89ABCDEF
    // Unaligned stores
    // examples:
    // reg =        11111111 01234567
    // mem @ x =             89ABCDEF

    // SWL x + 0 ->          01234567
    // SWL x + 1 ->          89012345
    // SWL x + 2 ->          89AB0123
    // SWL x + 3 ->          89ABCD01

    // SWR x + 0 ->          67ABCDEF
    // SWR x + 1 ->          4567CDEF
    // SWR x + 2 ->          234567EF
    // SWR x + 3 ->          01234567
    case InstrId::cpu_swl:
        print_line("do_swl(rdram, {}, {}{}, {}{})", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_swr:
        print_line("do_swr(rdram, {}, {}{}, {}{})", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
        break;

    // Branches
    case InstrId::cpu_jal:
        print_func_call(instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_jalr:
        // jalr can only be handled with $ra as the return address register
        if (rd != (int)rabbitizer::Registers::Cpu::GprO32::GPR_O32_ra) {
            fmt::print(stderr, "Invalid return address reg for jalr: f{}\n", rd);
            return false;
        }
        needs_link_branch = true;
        print_unconditional_branch("LOOKUP_FUNC({}{})(rdram, ctx)", ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_j:
    case InstrId::cpu_b:
        {
            uint32_t branch_target = instr.getBranchVramGeneric();
            if (branch_target == instr_vram) {
                print_line("pause_self(rdram)");
            }
            // Check if the branch is within this function
            else if (branch_target >= func.vram && branch_target < func_vram_end) {
                print_unconditional_branch("goto L_{:08X}", branch_target);
            }
            // This may be a tail call in the middle of the control flow due to a previous check
            // For example:
            // ```c
            // void test() {
            //     if (SOME_CONDITION) {
            //         do_a();
            //     } else {
            //         do_b();
            //     }
            // }
            // ```
            // FIXME: how to deal with static functions?
            else if (context.functions_by_vram.find(branch_target) != context.functions_by_vram.end()) {
                fmt::print("Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                print_func_call(branch_target, false);
                print_line("return");
            }
            else {
                fmt::print(stderr, "Unhandled branch in {} at 0x{:08X} to 0x{:08X}\n", func.name, instr_vram, branch_target);
                return false;
            }
        }
        break;
    case InstrId::cpu_jr:
        if (rs == (int)rabbitizer::Registers::Cpu::GprO32::GPR_O32_ra) {
            print_unconditional_branch("return");
        } else {
            auto jtbl_find_result = std::find_if(stats.jump_tables.begin(), stats.jump_tables.end(),
                [instr_vram](const RecompPort::JumpTable& jtbl) {
                    return jtbl.jr_vram == instr_vram;
                });

            if (jtbl_find_result != stats.jump_tables.end()) {
                const RecompPort::JumpTable& cur_jtbl = *jtbl_find_result;
                bool dummy_needs_link_branch, dummy_is_branch_likely;
                size_t next_reloc_index = reloc_index;
                uint32_t next_vram = instr_vram + 4;
                if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                    next_reloc_index++;
                }
                process_instruction(context, config, func, stats, skipped_insns, instr_index + 1, instructions, output_file, false, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out);
                print_indent();
                fmt::print(output_file, "switch (jr_addend_{:08X} >> 2) {{\n", cur_jtbl.jr_vram);
                for (size_t entry_index = 0; entry_index < cur_jtbl.entries.size(); entry_index++) {
                    print_indent();
                    print_line("case {}: goto L_{:08X}; break", entry_index, cur_jtbl.entries[entry_index]);
                }
                print_indent();
                print_line("default: switch_error(__func__, 0x{:08X}, 0x{:08X})", instr_vram, cur_jtbl.vram);
                print_indent();
                fmt::print(output_file, "}}\n");
                break;
            }

            auto jump_find_result = std::find_if(stats.absolute_jumps.begin(), stats.absolute_jumps.end(),
                [instr_vram](const RecompPort::AbsoluteJump& jump) {
                return jump.instruction_vram == instr_vram;
            });

            if (jump_find_result != stats.absolute_jumps.end()) {
                print_unconditional_branch("LOOKUP_FUNC({})(rdram, ctx)", (uint64_t)(int32_t)jump_find_result->jump_target);
                // jr doesn't link so it acts like a tail call, meaning we should return directly after the jump returns
                print_line("return");
                break;
            }

            bool is_tail_call = instr_vram == func_vram_end - 2 * sizeof(func.words[0]);
            if (is_tail_call) {
                fmt::print("Indirect tail call in {}\n", func.name);
                print_unconditional_branch("LOOKUP_FUNC({}{})(rdram, ctx)", ctx_gpr_prefix(rs), rs);
                print_line("return");
                break;
            }

            fmt::print(stderr, "No jump table found for jr at 0x{:08X} and not tail call\n", instr_vram);
        }
        break;
    case InstrId::cpu_syscall:
        print_line("recomp_syscall_handler(rdram, ctx, 0x{:08X})", instr_vram);
        // syscalls don't link, so treat it like a tail call
        print_line("return");
        break;
    case InstrId::cpu_break:
        print_line("do_break({})", instr_vram);
        break;

    // Cop1 loads/stores
    //case InstrId::cpu_dmfc1:
    //    if ((fs & 1) == 0) {
    //        // even fpr
    //        print_line("{}{} = ctx->f{}.u64", ctx_gpr_prefix(rt), rt, fs);
    //    } else {
    //        fmt::print(stderr, "Invalid operand for dmfc1: f{}\n", fs);
    //        return false;
    //    }
    //    break;
    case InstrId::cpu_swc1:
        if ((ft & 1) == 0) {
            // even fpr
            print_line("MEM_W({}, {}{}) = ctx->f{}.u32l", signed_imm_string, ctx_gpr_prefix(base), base, ft);
        } else {
            // odd fpr
            print_line("MEM_W({}, {}{}) = ctx->f_odd[({} - 1) * 2]", signed_imm_string, ctx_gpr_prefix(base), base, ft);
        }
        break;
    case InstrId::cpu_sdc1:
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("SD(ctx->f{}.u64, {}, {}{})", ft, signed_imm_string, ctx_gpr_prefix(base), base);
        break;

    // Cop1 compares
    // TODO allow NaN in ordered and unordered float comparisons, default to a compare result of 1 for ordered and 0 for unordered if a NaN is present
    case InstrId::cpu_c_lt_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl < ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_olt_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl < ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_ult_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl < ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_lt_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d < ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_olt_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d < ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_ult_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d < ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_le_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl <= ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_ole_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl <= ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_ule_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl <= ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_le_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d <= ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_ole_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d <= ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_ule_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d <= ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_eq_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl == ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_ueq_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl == ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_ngl_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl == ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_seq_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl == ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_eq_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d == ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_ueq_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d == ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_ngl_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d == ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_deq_d: // TODO rename to c_seq_d when fixed in rabbitizer
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d == ctx->f{}.d", fs, ft);
        break;

    // Cop1 arithmetic
    case InstrId::cpu_mov_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.fl = ctx->f{}.fl", fd, fs);
        break;
    case InstrId::cpu_mov_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.d = ctx->f{}.d", fd, fs);
        break;
    case InstrId::cpu_neg_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.fl)", fs);
        print_line("ctx->f{}.fl = -ctx->f{}.fl", fd, fs);
        break;
    case InstrId::cpu_neg_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.d)", fs);
        print_line("ctx->f{}.d = -ctx->f{}.d", fd, fs);
        break;
    case InstrId::cpu_abs_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.fl)", fs);
        print_line("ctx->f{}.fl = fabsf(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_abs_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.d)", fs);
        print_line("ctx->f{}.d = fabs(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_sqrt_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.fl)", fs);
        print_line("ctx->f{}.fl = sqrtf(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_sqrt_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.d)", fs);
        print_line("ctx->f{}.d = sqrt(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_add_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.fl); NAN_CHECK(ctx->f{}.fl)", fs, ft);
        print_line("ctx->f{}.fl = ctx->f{}.fl + ctx->f{}.fl", fd, fs, ft);
        break;
    case InstrId::cpu_add_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.d); NAN_CHECK(ctx->f{}.d)", fs, ft);
        print_line("ctx->f{}.d = ctx->f{}.d + ctx->f{}.d", fd, fs, ft);
        break;
    case InstrId::cpu_sub_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.fl); NAN_CHECK(ctx->f{}.fl)", fs, ft);
        print_line("ctx->f{}.fl = ctx->f{}.fl - ctx->f{}.fl", fd, fs, ft);
        break;
    case InstrId::cpu_sub_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.d); NAN_CHECK(ctx->f{}.d)", fs, ft);
        print_line("ctx->f{}.d = ctx->f{}.d - ctx->f{}.d", fd, fs, ft);
        break;
    case InstrId::cpu_mul_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.fl); NAN_CHECK(ctx->f{}.fl)", fs, ft);
        print_line("ctx->f{}.fl = MUL_S(ctx->f{}.fl, ctx->f{}.fl)", fd, fs, ft);
        break;
    case InstrId::cpu_mul_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.d); NAN_CHECK(ctx->f{}.d)", fs, ft);
        print_line("ctx->f{}.d = MUL_D(ctx->f{}.d, ctx->f{}.d)", fd, fs, ft);
        break;
    case InstrId::cpu_div_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.fl); NAN_CHECK(ctx->f{}.fl)", fs, ft);
        print_line("ctx->f{}.fl = DIV_S(ctx->f{}.fl, ctx->f{}.fl)", fd, fs, ft);
        break;
    case InstrId::cpu_div_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("NAN_CHECK(ctx->f{}.d); NAN_CHECK(ctx->f{}.d)", fs, ft);
        print_line("ctx->f{}.d = DIV_D(ctx->f{}.d, ctx->f{}.d)", fd, fs, ft);
        break;
    case InstrId::cpu_cvt_s_w:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.fl = CVT_S_W(ctx->f{}.u32l)", fd, fs);
        break;
    case InstrId::cpu_cvt_d_w:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.d = CVT_D_W(ctx->f{}.u32l)", fd, fs);
        break;
    case InstrId::cpu_cvt_d_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.fl)", fs);
        print_line("ctx->f{}.d = CVT_D_S(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_cvt_s_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.d)", fs);
        print_line("ctx->f{}.fl = CVT_S_D(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_cvt_d_l:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.d = CVT_D_L(ctx->f{}.u64)", fd, fs);
        break;
    case InstrId::cpu_cvt_l_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.d)", fs);
        print_line("ctx->f{}.u64 = CVT_L_D(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_cvt_s_l:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.fl = CVT_S_L(ctx->f{}.u64)", fd, fs);
        break;
    case InstrId::cpu_cvt_l_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("NAN_CHECK(ctx->f{}.fl)", fs);
        print_line("ctx->f{}.u64 = CVT_L_S(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_trunc_w_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = TRUNC_W_S(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_trunc_w_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = TRUNC_W_D(ctx->f{}.d)", fd, fs);
        break;
    //case InstrId::cpu_trunc_l_s:
    //    print_line("CHECK_FR(ctx, {})", fd);
    //    print_line("CHECK_FR(ctx, {})", fs);
    //    print_line("ctx->f{}.u64 = TRUNC_L_S(ctx->f{}.fl)", fd, fs);
    //    break;
    //case InstrId::cpu_trunc_l_d:
    //    print_line("CHECK_FR(ctx, {})", fd);
    //    print_line("CHECK_FR(ctx, {})", fs);
    //    print_line("ctx->f{}.u64 = TRUNC_L_D(ctx->f{}.d)", fd, fs);
    //    break;
    case InstrId::cpu_ctc1:
        if (cop1_cs != 31) {
            fmt::print(stderr, "Invalid FP control register for ctc1: {}\n", cop1_cs);
            return false;
        }
        print_line("rounding_mode = ({}{}) & 0x3", ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_cfc1:
        if (cop1_cs != 31) {
            fmt::print(stderr, "Invalid FP control register for cfc1: {}\n", cop1_cs);
            return false;
        }
        print_line("{}{} = rounding_mode", ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_cvt_w_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = CVT_W_S(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_cvt_w_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = CVT_W_D(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_round_w_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = lroundf(ctx->f{}.fl)", fd, fs);
        break;
    case InstrId::cpu_round_w_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = lround(ctx->f{}.d)", fd, fs);
        break;
    case InstrId::cpu_ceil_w_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = S32(ceilf(ctx->f{}.fl))", fd, fs);
        break;
    case InstrId::cpu_ceil_w_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = S32(ceil(ctx->f{}.d))", fd, fs);
        break;
    case InstrId::cpu_floor_w_s:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = S32(floorf(ctx->f{}.fl))", fd, fs);
        break;
    case InstrId::cpu_floor_w_d:
        print_line("CHECK_FR(ctx, {})", fd);
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("ctx->f{}.u32l = S32(floor(ctx->f{}.d))", fd, fs);
        break;
    default:
        handled = false;
        break;
    }

    CGenerator generator{};
    InstructionContext instruction_context{};
    instruction_context.rd = rd;
    instruction_context.rs = rs;
    instruction_context.rt = rt;
    instruction_context.sa = sa;
    instruction_context.fd = fd;
    instruction_context.fs = fs;
    instruction_context.ft = ft;
    instruction_context.cop1_cs = cop1_cs;
    instruction_context.imm16 = imm;
    instruction_context.reloc_type = reloc_type;
    instruction_context.reloc_section_index = func.section_index; // TODO allow relocs to other sections?
    instruction_context.reloc_target_section_offset = reloc_target_section_offset;

    auto find_binary_it = binary_ops.find(instr.getUniqueId());
    if (find_binary_it != binary_ops.end()) {
        print_indent();
        generator.process_binary_op(output_file, find_binary_it->second, instruction_context);
        handled = true;
    }

    auto find_unary_it = unary_ops.find(instr.getUniqueId());
    if (find_unary_it != unary_ops.end()) {
        print_indent();
        generator.process_unary_op(output_file, find_unary_it->second, instruction_context);
        handled = true;
    }

    auto find_conditional_branch_it = conditional_branch_ops.find(instr.getUniqueId());
    if (find_conditional_branch_it != conditional_branch_ops.end()) {
        print_indent();
        generator.emit_branch_condition(output_file, find_conditional_branch_it->second, instruction_context);

        print_indent();
        if (find_conditional_branch_it->second.link) {
            print_func_call(instr.getBranchVramGeneric());
        }
        else {
            print_branch((uint32_t)instr.getBranchVramGeneric());
        }

        generator.emit_branch_close(output_file);
        
        is_branch_likely = find_conditional_branch_it->second.likely;
        handled = true;
    }

    if (!handled) {
        fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
        return false;
    }

    // TODO is this used?
    if (emit_link_branch) {
        fmt::print(output_file, "    after_{}:\n", link_branch_index);
    }

    return true;
}

bool RecompPort::recompile_function(const RecompPort::Context& context, const RecompPort::Config& config, const RecompPort::Function& func, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool write_header) {
    //fmt::print("Recompiling {}\n", func.name);
    std::vector<rabbitizer::InstructionCpu> instructions;

    if (write_header) {
        // Write the file header
        fmt::print(output_file,
            "#include \"librecomp/recomp.h\"\n"
            "\n");
    }

    fmt::print(output_file,
        "void {}(uint8_t* rdram, recomp_context* ctx) {{\n"
        // these variables shouldn't need to be preserved across function boundaries, so make them local for more efficient output
        "    uint64_t hi = 0, lo = 0, result = 0;\n"
        "    unsigned int rounding_mode = DEFAULT_ROUNDING_MODE;\n"
        "    int c1cs = 0;\n", // cop1 conditional signal
        func.name);

    // Skip analysis and recompilation of this function is stubbed.
    if (!func.stubbed) {
        // Use a set to sort and deduplicate labels
        std::set<uint32_t> branch_labels;
        instructions.reserve(func.words.size());

        auto hook_find = func.function_hooks.find(-1);
        if (hook_find != func.function_hooks.end()) {
            fmt::print(output_file, "    {}\n", hook_find->second);
        }

        // First pass, disassemble each instruction and collect branch labels
        uint32_t vram = func.vram;
        for (uint32_t word : func.words) {
            const auto& instr = instructions.emplace_back(byteswap(word), vram);

            // If this is a branch or a direct jump, add it to the local label list
            if (instr.isBranch() || instr.getUniqueId() == rabbitizer::InstrId::UniqueId::cpu_j) {
                branch_labels.insert((uint32_t)instr.getBranchVramGeneric());
            }

            // Advance the vram address by the size of one instruction
            vram += 4;
        }

        // Analyze function
        RecompPort::FunctionStats stats{};
        if (!RecompPort::analyze_function(context, func, instructions, stats)) {
            fmt::print(stderr, "Failed to analyze {}\n", func.name);
            output_file.clear();
            return false;
        }

        std::unordered_set<uint32_t> skipped_insns{};

        // Add jump table labels into function
        for (const auto& jtbl : stats.jump_tables) {
            skipped_insns.insert(jtbl.lw_vram);
            for (uint32_t jtbl_entry : jtbl.entries) {
                branch_labels.insert(jtbl_entry);
            }
        }

        // Second pass, emit code for each instruction and emit labels
        auto cur_label = branch_labels.cbegin();
        vram = func.vram;
        int num_link_branches = 0;
        int num_likely_branches = 0;
        bool needs_link_branch = false;
        bool in_likely_delay_slot = false;
        const auto& section = context.sections[func.section_index];
        size_t reloc_index = 0;
        for (size_t instr_index = 0; instr_index < instructions.size(); ++instr_index) {
            bool had_link_branch = needs_link_branch;
            bool is_branch_likely = false;
            // If we're in the delay slot of a likely instruction, emit a goto to skip the instruction before any labels
            if (in_likely_delay_slot) {
                fmt::print(output_file, "    goto skip_{};\n", num_likely_branches);
            }
            // If there are any other branch labels to insert and we're at the next one, insert it
            if (cur_label != branch_labels.end() && vram >= *cur_label) {
                fmt::print(output_file, "L_{:08X}:\n", *cur_label);
                ++cur_label;
            }

            // Advance the reloc index until we reach the last one or until we get to/pass the current instruction
            while ((reloc_index + 1) < section.relocs.size() && section.relocs[reloc_index].address < vram) {
                reloc_index++;
            }

            // Process the current instruction and check for errors
            if (process_instruction(context, config, func, stats, skipped_insns, instr_index, instructions, output_file, false, needs_link_branch, num_link_branches, reloc_index, needs_link_branch, is_branch_likely, static_funcs_out) == false) {
                fmt::print(stderr, "Error in recompiling {}, clearing output file\n", func.name);
                output_file.clear();
                return false;
            }
            // If a link return branch was generated, advance the number of link return branches
            if (had_link_branch) {
                num_link_branches++;
            }
            // Now that the instruction has been processed, emit a skip label for the likely branch if needed
            if (in_likely_delay_slot) {
                fmt::print(output_file, "    skip_{}:\n", num_likely_branches);
                num_likely_branches++;
            }
            // Mark the next instruction as being in a likely delay slot if the 
            in_likely_delay_slot = is_branch_likely;
            // Advance the vram address by the size of one instruction
            vram += 4;
        }
    }

    // Terminate the function
    fmt::print(output_file, ";}}\n");
    
    return true;
}
