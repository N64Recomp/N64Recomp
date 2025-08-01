#include <cassert>
#include <fstream>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/generator.h"

struct BinaryOpFields { std::string func_string; std::string infix_string; };

static std::vector<BinaryOpFields> c_op_fields = []() {
    std::vector<BinaryOpFields> ret{};
    ret.resize(static_cast<size_t>(N64Recomp::BinaryOpType::COUNT));
    std::vector<char> ops_setup{};
    ops_setup.resize(static_cast<size_t>(N64Recomp::BinaryOpType::COUNT));

    auto setup_op = [&ret, &ops_setup](N64Recomp::BinaryOpType op_type, const std::string& func_string, const std::string& infix_string) {
        size_t index = static_cast<size_t>(op_type);
        // Prevent setting up an operation twice.
        assert(ops_setup[index] == false && "Operation already setup!");
        ops_setup[index] = true;
        ret[index] = { func_string, infix_string };
    };

    setup_op(N64Recomp::BinaryOpType::Add32,     "ADD32",  "");
    setup_op(N64Recomp::BinaryOpType::Sub32,     "SUB32",  "");
    setup_op(N64Recomp::BinaryOpType::Add64,     "",       "+");
    setup_op(N64Recomp::BinaryOpType::Sub64,     "",       "-");
    setup_op(N64Recomp::BinaryOpType::And64,     "",       "&");
    setup_op(N64Recomp::BinaryOpType::AddFloat,  "",       "+");
    setup_op(N64Recomp::BinaryOpType::AddDouble, "",       "+");
    setup_op(N64Recomp::BinaryOpType::SubFloat,  "",       "-");
    setup_op(N64Recomp::BinaryOpType::SubDouble, "",       "-");
    setup_op(N64Recomp::BinaryOpType::MulFloat,  "MUL_S",  "");
    setup_op(N64Recomp::BinaryOpType::MulDouble, "MUL_D",  "");
    setup_op(N64Recomp::BinaryOpType::DivFloat,  "DIV_S",  "");
    setup_op(N64Recomp::BinaryOpType::DivDouble, "DIV_D",  "");
    setup_op(N64Recomp::BinaryOpType::Or64,      "",       "|");
    setup_op(N64Recomp::BinaryOpType::Nor64,     "~",      "|");
    setup_op(N64Recomp::BinaryOpType::Xor64,     "",       "^");
    setup_op(N64Recomp::BinaryOpType::Sll32,     "S32",    "<<");
    setup_op(N64Recomp::BinaryOpType::Sll64,     "",       "<<");
    setup_op(N64Recomp::BinaryOpType::Srl32,     "S32",    ">>");
    setup_op(N64Recomp::BinaryOpType::Srl64,     "",       ">>");
    setup_op(N64Recomp::BinaryOpType::Sra32,     "S32",    ">>"); // Arithmetic aspect will be taken care of by unary op for first operand.
    setup_op(N64Recomp::BinaryOpType::Sra64,     "",       ">>"); // Arithmetic aspect will be taken care of by unary op for first operand.
    setup_op(N64Recomp::BinaryOpType::Equal,     "",       "==");
    setup_op(N64Recomp::BinaryOpType::EqualFloat,"",       "==");
    setup_op(N64Recomp::BinaryOpType::EqualDouble,"",      "==");
    setup_op(N64Recomp::BinaryOpType::NotEqual,  "",       "!=");
    setup_op(N64Recomp::BinaryOpType::Less,      "",       "<");
    setup_op(N64Recomp::BinaryOpType::LessFloat, "",       "<");
    setup_op(N64Recomp::BinaryOpType::LessDouble,"",       "<");
    setup_op(N64Recomp::BinaryOpType::LessEq,    "",       "<=");
    setup_op(N64Recomp::BinaryOpType::LessEqFloat,"",      "<=");
    setup_op(N64Recomp::BinaryOpType::LessEqDouble,"",     "<=");
    setup_op(N64Recomp::BinaryOpType::Greater,   "",       ">");
    setup_op(N64Recomp::BinaryOpType::GreaterEq, "",       ">=");
    setup_op(N64Recomp::BinaryOpType::LD,        "LD",     "");
    setup_op(N64Recomp::BinaryOpType::LW,        "MEM_W",  "");
    setup_op(N64Recomp::BinaryOpType::LWU,       "MEM_WU", "");
    setup_op(N64Recomp::BinaryOpType::LH,        "MEM_H",  "");
    setup_op(N64Recomp::BinaryOpType::LHU,       "MEM_HU", "");
    setup_op(N64Recomp::BinaryOpType::LB,        "MEM_B",  "");
    setup_op(N64Recomp::BinaryOpType::LBU,       "MEM_BU", "");
    setup_op(N64Recomp::BinaryOpType::LDL,       "do_ldl", "");
    setup_op(N64Recomp::BinaryOpType::LDR,       "do_ldr", "");
    setup_op(N64Recomp::BinaryOpType::LWL,       "do_lwl", "");
    setup_op(N64Recomp::BinaryOpType::LWR,       "do_lwr", "");
    setup_op(N64Recomp::BinaryOpType::True,      "", "");
    setup_op(N64Recomp::BinaryOpType::False,     "", "");

    // Ensure every operation has been setup.
    for (char is_set : ops_setup) {
        assert(is_set && "Operation has not been setup!");
    }

    return ret;
}();

static std::string gpr_to_string(int gpr_index) {
    if (gpr_index == 0) {
        return "0";
    }
    return fmt::format("ctx->r{}", gpr_index);
}

static std::string fpr_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.fl", fpr_index);
}

static std::string fpr_double_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.d", fpr_index);
}

static std::string fpr_u32l_to_string(int fpr_index) {
    if (fpr_index & 1) {
        return fmt::format("ctx->f_odd[({} - 1) * 2]", fpr_index);
    }
    else {
        return fmt::format("ctx->f{}.u32l", fpr_index);
    }
}

static std::string fpr_u64_to_string(int fpr_index) {
    return fmt::format("ctx->f{}.u64", fpr_index);
}

static std::string unsigned_reloc(const N64Recomp::InstructionContext& context) {
    switch (context.reloc_type) {
        case N64Recomp::RelocType::R_MIPS_HI16:
            return fmt::format("{}RELOC_HI16({}, {:#X})",
                context.reloc_tag_as_reference ? "REF_" : "", context.reloc_section_index, context.reloc_target_section_offset);
        case N64Recomp::RelocType::R_MIPS_LO16:
            return fmt::format("{}RELOC_LO16({}, {:#X})",
                context.reloc_tag_as_reference ? "REF_" : "", context.reloc_section_index, context.reloc_target_section_offset);
        default:
            throw std::runtime_error(fmt::format("Unexpected reloc type {}\n", static_cast<int>(context.reloc_type)));
    }
}

static std::string signed_reloc(const N64Recomp::InstructionContext& context) {
    return "(int16_t)" + unsigned_reloc(context);
}

void N64Recomp::CGenerator::get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const {
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
            if (context.reloc_type != N64Recomp::RelocType::R_MIPS_NONE) {
                operand_string = unsigned_reloc(context);
            }
            else {
                operand_string = fmt::format("{:#X}", context.imm16);
            }
            break;
        case Operand::ImmS16:
            if (context.reloc_type != N64Recomp::RelocType::R_MIPS_NONE) {
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
        case UnaryOpType::NegateFloat:
            operand_string = "-" + operand_string;
            break;
        case UnaryOpType::NegateDouble:
            operand_string = "-" + operand_string;
            break;
        case UnaryOpType::AbsFloat:
            operand_string = "fabsf(" + operand_string + ")";
            break;
        case UnaryOpType::AbsDouble:
            operand_string = "fabs(" + operand_string + ")";
            break;
        case UnaryOpType::SqrtFloat:
            operand_string = "sqrtf(" + operand_string + ")";
            break;
        case UnaryOpType::SqrtDouble:
            operand_string = "sqrt(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertSFromW:
            operand_string = "CVT_S_W(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertWFromS:
            operand_string = "CVT_W_S(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertDFromW:
            operand_string = "CVT_D_W(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertWFromD:
            operand_string = "CVT_W_D(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertDFromS:
            operand_string = "CVT_D_S(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertSFromD:
            operand_string = "CVT_S_D(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertDFromL:
            operand_string = "CVT_D_L(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertLFromD:
            operand_string = "CVT_L_D(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertSFromL:
            operand_string = "CVT_S_L(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertLFromS:
            operand_string = "CVT_L_S(" + operand_string + ")";
            break;
        case UnaryOpType::TruncateWFromS:
            operand_string = "TRUNC_W_S(" + operand_string + ")";
            break;
        case UnaryOpType::TruncateWFromD:
            operand_string = "TRUNC_W_D(" + operand_string + ")";
            break;
        case UnaryOpType::TruncateLFromS:
            operand_string = "TRUNC_L_S(" + operand_string + ")";
            break;
        case UnaryOpType::TruncateLFromD:
            operand_string = "TRUNC_L_D(" + operand_string + ")";
            break;
        // TODO these four operations should use banker's rounding, but roundeven is C23 so it's unavailable here.
        case UnaryOpType::RoundWFromS:
            operand_string = "lroundf(" + operand_string + ")";
            break;
        case UnaryOpType::RoundWFromD:
            operand_string = "lround(" + operand_string + ")";
            break;
        case UnaryOpType::RoundLFromS:
            operand_string = "llroundf(" + operand_string + ")";
            break;
        case UnaryOpType::RoundLFromD:
            operand_string = "llround(" + operand_string + ")";
            break;
        case UnaryOpType::CeilWFromS:
            operand_string = "S32(ceilf(" + operand_string + "))";
            break;
        case UnaryOpType::CeilWFromD:
            operand_string = "S32(ceil(" + operand_string + "))";
            break;
        case UnaryOpType::CeilLFromS:
            operand_string = "S64(ceilf(" + operand_string + "))";
            break;
        case UnaryOpType::CeilLFromD:
            operand_string = "S64(ceil(" + operand_string + "))";
            break;
        case UnaryOpType::FloorWFromS:
            operand_string = "S32(floorf(" + operand_string + "))";
            break;
        case UnaryOpType::FloorWFromD:
            operand_string = "S32(floor(" + operand_string + "))";
            break;
        case UnaryOpType::FloorLFromS:
            operand_string = "S64(floorf(" + operand_string + "))";
            break;
        case UnaryOpType::FloorLFromD:
            operand_string = "S64(floor(" + operand_string + "))";
            break;
    }
}

void N64Recomp::CGenerator::get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const {
    func_string = c_op_fields[static_cast<size_t>(op_type)].func_string;
    infix_string = c_op_fields[static_cast<size_t>(op_type)].infix_string;
}

void N64Recomp::CGenerator::get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const {
    thread_local std::string input_a{};
    thread_local std::string input_b{};
    thread_local std::string func_string{};
    thread_local std::string infix_string{};
    get_operand_string(operands.operands[0], operands.operand_operations[0], ctx, input_a);
    get_operand_string(operands.operands[1], operands.operand_operations[1], ctx, input_b);
    get_notation(type, func_string, infix_string);
    
    // These cases aren't strictly necessary and are just here for parity with the old recompiler output.
    if (type == BinaryOpType::Less && !((operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) || (operands.operands[0] == Operand::Fs || operands.operands[0] == Operand::FsDouble))) {
        expr_string = fmt::format("{} {} {} ? 1 : 0", input_a, infix_string, input_b);
    }
    else if (type == BinaryOpType::Equal && operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) {
        expr_string = "!" + input_a;
    }
    else if (type == BinaryOpType::NotEqual && operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) {
        expr_string = input_a;
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

void N64Recomp::CGenerator::emit_function_start(const std::string& function_name, size_t func_index) const {
    (void)func_index;
    fmt::print(output_file,
        "RECOMP_FUNC void {}(uint8_t* rdram, recomp_context* ctx) {{\n"
        // these variables shouldn't need to be preserved across function boundaries, so make them local for more efficient output
        "    uint64_t hi = 0, lo = 0, result = 0;\n"
        "    int c1cs = 0;\n", // cop1 conditional signal
        function_name);
}

void N64Recomp::CGenerator::emit_function_end() const {
    fmt::print(output_file, ";}}\n");
}

void N64Recomp::CGenerator::emit_function_call_lookup(uint32_t addr) const {
    fmt::print(output_file, "LOOKUP_FUNC(0x{:08X})(rdram, ctx);\n", addr);
}

void N64Recomp::CGenerator::emit_function_call_by_register(int reg) const {
    if (reg == -1) {
        // Use the temp variable for jalr target
        fmt::print(output_file, "LOOKUP_FUNC({})(rdram, ctx);\n", "jalr_target");
    } else if (reg == -2) {
        // Use the temp variable for jr target
        fmt::print(output_file, "LOOKUP_FUNC({})(rdram, ctx);\n", "jr_target");
    } else {
        fmt::print(output_file, "LOOKUP_FUNC({})(rdram, ctx);\n", gpr_to_string(reg));
    }
}

void N64Recomp::CGenerator::emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const {
    (void)target_section_offset;
    const N64Recomp::ReferenceSymbol& sym = context.get_reference_symbol(section_index, symbol_index);
    fmt::print(output_file, "{}(rdram, ctx);\n", sym.name);
}

void N64Recomp::CGenerator::emit_function_call(const Context& context, size_t function_index) const {
    fmt::print(output_file, "{}(rdram, ctx);\n", context.functions[function_index].name);
}

void N64Recomp::CGenerator::emit_named_function_call(const std::string& function_name) const {
    fmt::print(output_file, "{}(rdram, ctx);\n", function_name);
}

void N64Recomp::CGenerator::emit_goto(const std::string& target) const {
    fmt::print(output_file,
        "    goto {};\n", target);
}

void N64Recomp::CGenerator::emit_label(const std::string& label_name) const {
    fmt::print(output_file,
        "{}:\n", label_name);
}

void N64Recomp::CGenerator::emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const {
    std::string jump_variable = fmt::format("jr_addend_{:08X}", jtbl.jr_vram);
    fmt::print(output_file, "gpr {} = {};\n", jump_variable, gpr_to_string(reg));
}

void N64Recomp::CGenerator::emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string expr_string{};
    get_binary_expr_string(op.comparison, op.operands, ctx, "", expr_string);
    fmt::print(output_file, "if ({}) {{\n", expr_string);
}

void N64Recomp::CGenerator::emit_branch_close() const {
    fmt::print(output_file, "}}\n");
}

void N64Recomp::CGenerator::emit_switch_close() const {
    fmt::print(output_file, "}}\n");
}

void N64Recomp::CGenerator::emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const {
    (void)recompiler_context;
    (void)reg;
    // TODO generate code to subtract the jump table address from the register's value instead.
    // Once that's done, the addend temp can be deleted to simplify the generator interface.
    std::string jump_variable = fmt::format("jr_addend_{:08X}", jtbl.jr_vram);

    fmt::print(output_file, "switch ({} >> 2) {{\n", jump_variable);
}

void N64Recomp::CGenerator::emit_case(int case_index, const std::string& target_label) const {
    fmt::print(output_file, "case {}: goto {}; break;\n", case_index, target_label);
}

void N64Recomp::CGenerator::emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const {
    fmt::print(output_file, "default: switch_error(__func__, 0x{:08X}, 0x{:08X});\n", instr_vram, jtbl_vram);
}

void N64Recomp::CGenerator::emit_return(const Context& context, size_t func_index) const {
    (void)func_index;
    if (context.trace_mode) {
        fmt::print(output_file, "TRACE_RETURN()\n    ");
    }
    fmt::print(output_file, "return;\n");
}

void N64Recomp::CGenerator::emit_check_fr(int fpr) const {
    fmt::print(output_file, "CHECK_FR(ctx, {});\n    ", fpr);
}

void N64Recomp::CGenerator::emit_check_nan(int fpr, bool is_double) const {
    fmt::print(output_file, "NAN_CHECK(ctx->f{}.{}); ", fpr, is_double ? "d" : "fl");
}

void N64Recomp::CGenerator::emit_cop0_status_read(int reg) const {
    fmt::print(output_file, "{} = cop0_status_read(ctx);\n", gpr_to_string(reg));
}

void N64Recomp::CGenerator::emit_cop0_status_write(int reg) const {
    fmt::print(output_file, "cop0_status_write(ctx, {});", gpr_to_string(reg));
}

void N64Recomp::CGenerator::emit_cop1_cs_read(int reg) const {
    fmt::print(output_file, "{} = get_cop1_cs();\n", gpr_to_string(reg));
}

void N64Recomp::CGenerator::emit_cop1_cs_write(int reg) const {
    fmt::print(output_file, "set_cop1_cs({});\n", gpr_to_string(reg));
}

void N64Recomp::CGenerator::emit_muldiv(InstrId instr_id, int reg1, int reg2) const {
    switch (instr_id) {
        case InstrId::cpu_mult:
            fmt::print(output_file, "result = S64(S32({})) * S64(S32({})); lo = S32(result >> 0); hi = S32(result >> 32);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_dmult:
            fmt::print(output_file, "DMULT(S64({}), S64({}), &lo, &hi);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_multu:
            fmt::print(output_file, "result = U64(U32({})) * U64(U32({})); lo = S32(result >> 0); hi = S32(result >> 32);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_dmultu:
            fmt::print(output_file, "DMULTU(U64({}), U64({}), &lo, &hi);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_div:
            // Cast to 64-bits before division to prevent artihmetic exception for s32(0x80000000) / -1
            fmt::print(output_file, "lo = S32(S64(S32({0})) / S64(S32({1}))); hi = S32(S64(S32({0})) % S64(S32({1})));\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_ddiv:
            fmt::print(output_file, "DDIV(S64({}), S64({}), &lo, &hi);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_divu:
            fmt::print(output_file, "lo = S32(U32({0}) / U32({1})); hi = S32(U32({0}) % U32({1}));\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        case InstrId::cpu_ddivu:
            fmt::print(output_file, "DDIVU(U64({}), U64({}), &lo, &hi);\n", gpr_to_string(reg1), gpr_to_string(reg2));
            break;
        default:
            assert(false);
            break;
    }
}

void N64Recomp::CGenerator::emit_syscall(uint32_t instr_vram) const {
    fmt::print(output_file, "recomp_syscall_handler(rdram, ctx, 0x{:08X});\n", instr_vram);
}

void N64Recomp::CGenerator::emit_do_break(uint32_t instr_vram) const {
    fmt::print(output_file, "do_break({});\n", instr_vram);
}

void N64Recomp::CGenerator::emit_pause_self() const {
    fmt::print(output_file, "pause_self(rdram);\n");
}

void N64Recomp::CGenerator::emit_trigger_event(uint32_t event_index) const {
    fmt::print(output_file, "recomp_trigger_event(rdram, ctx, base_event_index + {});\n", event_index);
}

void N64Recomp::CGenerator::emit_comment(const std::string& comment) const {
    fmt::print(output_file, "// {}\n", comment);
}

void N64Recomp::CGenerator::process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string expression{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_binary_expr_string(op.type, op.operands, ctx, output, expression);
    fmt::print(output_file, "{} = {};\n", output, expression);
}

void N64Recomp::CGenerator::process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string input{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_operand_string(op.input, op.operation, ctx, input);
    fmt::print(output_file, "{} = {};\n", output, input);
}

void N64Recomp::CGenerator::process_store_op(const StoreOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string base_str{};
    thread_local std::string imm_str{};
    thread_local std::string value_input{};
    get_operand_string(Operand::Base, UnaryOpType::None, ctx, base_str);
    get_operand_string(Operand::ImmS16, UnaryOpType::None, ctx, imm_str);
    get_operand_string(op.value_input, UnaryOpType::None, ctx, value_input);

    enum class StoreSyntax {
        Func,
        FuncWithRdram,
        Assignment,
    };

    StoreSyntax syntax;
    std::string func_text;

    switch (op.type) {
        case StoreOpType::SD:
            func_text = "SD";
            syntax = StoreSyntax::Func;
            break;
        case StoreOpType::SDL:
            func_text = "do_sdl";
            syntax = StoreSyntax::FuncWithRdram;
            break;
        case StoreOpType::SDR:
            func_text = "do_sdr";
            syntax = StoreSyntax::FuncWithRdram;
            break;
        case StoreOpType::SW:
            func_text = "MEM_W";
            syntax = StoreSyntax::Assignment;
            break;
        case StoreOpType::SWL:
            func_text = "do_swl";
            syntax = StoreSyntax::FuncWithRdram;
            break;
        case StoreOpType::SWR:
            func_text = "do_swr";
            syntax = StoreSyntax::FuncWithRdram;
            break;
        case StoreOpType::SH:
            func_text = "MEM_H";
            syntax = StoreSyntax::Assignment;
            break;
        case StoreOpType::SB:
            func_text = "MEM_B";
            syntax = StoreSyntax::Assignment;
            break;
        case StoreOpType::SDC1:
            func_text = "SD";
            syntax = StoreSyntax::Func;
            break;
        case StoreOpType::SWC1:
            func_text = "MEM_W";
            syntax = StoreSyntax::Assignment;
            break;
        default:
            throw std::runtime_error("Unhandled store op");
    }

    switch (syntax) {
        case StoreSyntax::Func:
            fmt::print(output_file, "{}({}, {}, {});\n", func_text, value_input, imm_str, base_str);
            break;
        case StoreSyntax::FuncWithRdram:
            fmt::print(output_file, "{}(rdram, {}, {}, {});\n", func_text, imm_str, base_str, value_input);
            break;
        case StoreSyntax::Assignment:
            fmt::print(output_file, "{}({}, {}) = {};\n", func_text, imm_str, base_str, value_input);
            break;
    }
}
