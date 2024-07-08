#include <cassert>
#include <fstream>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "generator.h"

struct BinaryOpFields { std::string func_string; std::string infix_string; };

std::vector<BinaryOpFields> c_op_fields = []() {
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
    setup_op(N64Recomp::BinaryOpType::NotEqual,  "",       "!=");
    setup_op(N64Recomp::BinaryOpType::Less,      "",       "<");
    setup_op(N64Recomp::BinaryOpType::LessEq,    "",       "<=");
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

std::string unsigned_reloc(const N64Recomp::InstructionContext& context) {
    switch (context.reloc_type) {
        case N64Recomp::RelocType::R_MIPS_HI16:
            return fmt::format("RELOC_HI16({}, {:#X})", context.reloc_section_index, context.reloc_target_section_offset);
        case N64Recomp::RelocType::R_MIPS_LO16:
            return fmt::format("RELOC_LO16({}, {:#X})", context.reloc_section_index, context.reloc_target_section_offset);
        default:
            throw std::runtime_error(fmt::format("Unexpected reloc type {}\n", static_cast<int>(context.reloc_type)));
    }
}

std::string signed_reloc(const N64Recomp::InstructionContext& context) {
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
        case UnaryOpType::Negate:
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
        case UnaryOpType::RoundWFromS:
            operand_string = "lroundf(" + operand_string + ")";
            break;
        case UnaryOpType::RoundWFromD:
            operand_string = "lround(" + operand_string + ")";
            break;
        case UnaryOpType::CeilWFromS:
            operand_string = "S32(ceilf(" + operand_string + "))";
            break;
        case UnaryOpType::CeilWFromD:
            operand_string = "S32(ceil(" + operand_string + "))";
            break;
        case UnaryOpType::FloorWFromS:
            operand_string = "S32(floorf(" + operand_string + "))";
            break;
        case UnaryOpType::FloorWFromD:
            operand_string = "S32(floor(" + operand_string + "))";
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
    bool is_infix;
    get_operand_string(operands.operands[0], operands.operand_operations[0], ctx, input_a);
    get_operand_string(operands.operands[1], operands.operand_operations[1], ctx, input_b);
    get_notation(type, func_string, infix_string);
    
    // These cases aren't strictly necessary and are just here for parity with the old recompiler output.
    if (type == BinaryOpType::Less && !((operands.operands[1] == Operand::Zero && operands.operand_operations[1] == UnaryOpType::None) || (operands.operands[0] == Operand::Fs || operands.operands[0] == Operand::FsDouble))) {
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

void N64Recomp::CGenerator::emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string expr_string{};
    get_binary_expr_string(op.comparison, op.operands, ctx, "", expr_string);
    fmt::print(output_file, "if ({}) {{\n", expr_string);
}

void N64Recomp::CGenerator::emit_branch_close(std::ostream& output_file) const {
    fmt::print(output_file, "    }}\n");
}

void N64Recomp::CGenerator::emit_check_fr(std::ostream& output_file, int fpr) const {
    fmt::print(output_file, "CHECK_FR(ctx, {});\n    ", fpr);
}

void N64Recomp::CGenerator::emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const {
    fmt::print(output_file, "NAN_CHECK(ctx->f{}.{}); ", fpr, is_double ? "d" : "fl");
}

void N64Recomp::CGenerator::process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string expression{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_binary_expr_string(op.type, op.operands, ctx, output, expression);
    fmt::print(output_file, "{} = {};\n", output, expression);
}

void N64Recomp::CGenerator::process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string input{};
    bool is_infix;
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_operand_string(op.input, op.operation, ctx, input);
    fmt::print(output_file, "{} = {};\n", output, input);
}

void N64Recomp::CGenerator::process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string base_str{};
    thread_local std::string imm_str{};
    thread_local std::string value_input{};
    bool is_infix;
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
