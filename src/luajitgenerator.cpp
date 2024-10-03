#include <cassert>
#include <fstream>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "generator.h"

struct BinaryOpFields { std::string func_string; std::string infix_string; };

static std::vector<BinaryOpFields> luajit_op_fields = []() {
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

    setup_op(N64Recomp::BinaryOpType::Add32,     "ADD32",       "");
    setup_op(N64Recomp::BinaryOpType::Sub32,     "SUB32",       "");
    setup_op(N64Recomp::BinaryOpType::Add64,     "",            "+");
    setup_op(N64Recomp::BinaryOpType::Sub64,     "",            "-");
    setup_op(N64Recomp::BinaryOpType::And64,     "bit.band",    "");
    setup_op(N64Recomp::BinaryOpType::AddFloat,  "",            "+");
    setup_op(N64Recomp::BinaryOpType::AddDouble, "",            "+");
    setup_op(N64Recomp::BinaryOpType::SubFloat,  "",            "-");
    setup_op(N64Recomp::BinaryOpType::SubDouble, "",            "-");
    setup_op(N64Recomp::BinaryOpType::MulFloat,  "MUL_S",       "");
    setup_op(N64Recomp::BinaryOpType::MulDouble, "MUL_D",       "");
    setup_op(N64Recomp::BinaryOpType::DivFloat,  "DIV_S",       "");
    setup_op(N64Recomp::BinaryOpType::DivDouble, "DIV_D",       "");
    setup_op(N64Recomp::BinaryOpType::Or64,      "bit.bor",     "");
    setup_op(N64Recomp::BinaryOpType::Nor64,     "NOR",         "");
    setup_op(N64Recomp::BinaryOpType::Xor64,     "bit.bxor",    "");
    setup_op(N64Recomp::BinaryOpType::Sll32,     "SLL32",       "");
    setup_op(N64Recomp::BinaryOpType::Sll64,     "bit.lshift",  "");
    setup_op(N64Recomp::BinaryOpType::Srl32,     "SRL32",       "");
    setup_op(N64Recomp::BinaryOpType::Srl64,     "bit.rshift",  "");
    setup_op(N64Recomp::BinaryOpType::Sra32,     "SRA32",       "");
    setup_op(N64Recomp::BinaryOpType::Sra64,     "bit.arshift", "");
    setup_op(N64Recomp::BinaryOpType::Equal,     "",            "==");
    setup_op(N64Recomp::BinaryOpType::NotEqual,  "",            "~=");
    setup_op(N64Recomp::BinaryOpType::Less,      "",            "<");
    setup_op(N64Recomp::BinaryOpType::LessEq,    "",            "<=");
    setup_op(N64Recomp::BinaryOpType::Greater,   "",            ">");
    setup_op(N64Recomp::BinaryOpType::GreaterEq, "",            ">=");
    setup_op(N64Recomp::BinaryOpType::LD,        "MEM_LD",      "");
    setup_op(N64Recomp::BinaryOpType::LW,        "MEM_LW",      "");
    setup_op(N64Recomp::BinaryOpType::LWU,       "MEM_LWU",     "");
    setup_op(N64Recomp::BinaryOpType::LH,        "MEM_LH",      "");
    setup_op(N64Recomp::BinaryOpType::LHU,       "MEM_LHU",     "");
    setup_op(N64Recomp::BinaryOpType::LB,        "MEM_LB",      "");
    setup_op(N64Recomp::BinaryOpType::LBU,       "MEM_LBU",     "");
    setup_op(N64Recomp::BinaryOpType::LDL,       "MEM_LDL",     "");
    setup_op(N64Recomp::BinaryOpType::LDR,       "MEM_LDR",     "");
    setup_op(N64Recomp::BinaryOpType::LWL,       "MEM_LWL",     "");
    setup_op(N64Recomp::BinaryOpType::LWR,       "MEM_LWR",     "");
    setup_op(N64Recomp::BinaryOpType::True,      "",            "");
    setup_op(N64Recomp::BinaryOpType::False,     "",            "");

    // Ensure every operation has been setup.
    for (char is_set : ops_setup) {
        assert(is_set && "Operation has not been setup!");
    }

    return ret;
}();

static std::string gpr_to_string(int gpr_index) {
    if (gpr_index == 0) {
        return "0ULL";
    }
    return fmt::format("ctx.r{}", gpr_index);
}

static std::string fpr_to_string(int fpr_index) {
    return fmt::format("ctx.f{}.fl", fpr_index);
}

static std::string fpr_double_to_string(int fpr_index) {
    return fmt::format("ctx.f{}.d", fpr_index);
}

static std::string fpr_u32l_to_string(int fpr_index) {
    if (fpr_index & 1) {
        return fmt::format("ctx.f_odd[({} - 1) * 2]", fpr_index);
    }
    else {
        return fmt::format("ctx.f{}.u32l", fpr_index);
    }
}

static std::string fpr_u64_to_string(int fpr_index) {
    return fmt::format("ctx.f{}.u64", fpr_index);
}

static std::string unsigned_reloc(const N64Recomp::InstructionContext& context) {
    switch (context.reloc_type) {
        case N64Recomp::RelocType::R_MIPS_HI16:
            return fmt::format("{}RELOC_HI16({}, {:#X}ULL)",
                context.reloc_tag_as_reference ? "REF_" : "", context.reloc_section_index, context.reloc_target_section_offset);
        case N64Recomp::RelocType::R_MIPS_LO16:
            return fmt::format("{}RELOC_LO16({}, {:#X}ULL)",
                context.reloc_tag_as_reference ? "REF_" : "", context.reloc_section_index, context.reloc_target_section_offset);
        default:
            throw std::runtime_error(fmt::format("Unexpected reloc type {}\n", static_cast<int>(context.reloc_type)));
    }
}

static std::string signed_reloc(const N64Recomp::InstructionContext& context) {
    return "(int16_t)" + unsigned_reloc(context);
}

void N64Recomp::LuajitGenerator::get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const {
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
                operand_string = fmt::format("{:#X}ULL", context.imm16);
            }
            break;
        case Operand::ImmS16:
            if (context.reloc_type != N64Recomp::RelocType::R_MIPS_NONE) {
                operand_string = signed_reloc(context);
            }
            else {
                operand_string = fmt::format("{:#X}ULL", (int16_t)context.imm16);
            }
            break;
        case Operand::Sa:
            operand_string = std::to_string(context.sa);
            break;
        case Operand::Sa32:
            operand_string = fmt::format("({} + 32ULL)", context.sa);
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
            operand_string = "0ULL";
            break;
    }
    switch (operation) {
        case UnaryOpType::None:
            break;
        case UnaryOpType::ToS32:
        case UnaryOpType::ToInt32:
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
            operand_string = "S32(bit.lshift(" + operand_string + "), 16ULL)"; 
            break;
        case UnaryOpType::Mask5:
            operand_string = "(bit.band(" + operand_string + "), 31ULL)";
            break;
        case UnaryOpType::Mask6:
            operand_string = "(bit.band(" + operand_string + "), 63ULL)";
            break;
        case UnaryOpType::NegateFloat:
            operand_string = "-" + operand_string;
            break;
        case UnaryOpType::NegateDouble:
            operand_string = "-" + operand_string;
            break;
        case UnaryOpType::AbsFloat:
            operand_string = "ABSF(" + operand_string + ")";
            break;
        case UnaryOpType::AbsDouble:
            operand_string = "ABS(" + operand_string + ")";
            break;
        case UnaryOpType::SqrtFloat:
            operand_string = "SQRTF(" + operand_string + ")";
            break;
        case UnaryOpType::SqrtDouble:
            operand_string = "SQRT(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertSFromW:
            operand_string = "CVT_S_W(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertWFromS:
            operand_string = "CVT_W_S(" + operand_string + ", rounding_mode)";
            break;
        case UnaryOpType::ConvertDFromW:
            operand_string = "CVT_D_W(" + operand_string + ")";
            break;
        case UnaryOpType::ConvertWFromD:
            operand_string = "CVT_W_D(" + operand_string + ", rounding_mode)";
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
            operand_string = "ROUND_W_S(" + operand_string + ")";
            break;
        case UnaryOpType::RoundWFromD:
            operand_string = "ROUND_W_D(" + operand_string + ")";
            break;
        case UnaryOpType::CeilWFromS:
            operand_string = "CEILF(" + operand_string + ")";
            break;
        case UnaryOpType::CeilWFromD:
            operand_string = "CEIL(" + operand_string + ")";
            break;
        case UnaryOpType::FloorWFromS:
            operand_string = "FLOORF(" + operand_string + ")";
            break;
        case UnaryOpType::FloorWFromD:
            operand_string = "FLOOR(" + operand_string + ")";
            break;
    }
}

void N64Recomp::LuajitGenerator::get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const {
    func_string = luajit_op_fields[static_cast<size_t>(op_type)].func_string;
    infix_string = luajit_op_fields[static_cast<size_t>(op_type)].infix_string;
}

void N64Recomp::LuajitGenerator::get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const {
    thread_local std::string input_a{};
    thread_local std::string input_b{};
    thread_local std::string func_string{};
    thread_local std::string infix_string{};
    bool is_infix;
    get_operand_string(operands.operands[0], operands.operand_operations[0], ctx, input_a);
    get_operand_string(operands.operands[1], operands.operand_operations[1], ctx, input_b);
    get_notation(type, func_string, infix_string);
    
    if (!func_string.empty() && !infix_string.empty()) {
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
            expr_string = "true";
        }
        else if (type == BinaryOpType::False) {
            expr_string = "false";
        }
        assert(false && "Binary operation must have either a function or infix!");
    }
}


void N64Recomp::LuajitGenerator::process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string expression{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_binary_expr_string(op.type, op.operands, ctx, output, expression);

    // Explicitly convert coprocessor 1 compare results into a number to ensure comparisons work correctly with it.
    if (op.output == Operand::Cop1cs) {
        expression = "BOOL_TO_NUM(" + expression + ")";
    }

    fmt::print(output_file, "{} = {}\n", output, expression);
}

void N64Recomp::LuajitGenerator::process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string output{};
    thread_local std::string input{};
    bool is_infix;
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_operand_string(op.input, op.operation, ctx, input);
    fmt::print(output_file, "{} = {}\n", output, input);
}

void N64Recomp::LuajitGenerator::process_store_op(const StoreOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string base_str{};
    thread_local std::string imm_str{};
    thread_local std::string value_input{};
    bool is_infix;
    get_operand_string(Operand::Base, UnaryOpType::None, ctx, base_str);
    get_operand_string(Operand::ImmS16, UnaryOpType::None, ctx, imm_str);
    get_operand_string(op.value_input, UnaryOpType::None, ctx, value_input);

    std::string func_text;

    switch (op.type) {
        case StoreOpType::SD:
            func_text = "MEM_SD";
            break;
        case StoreOpType::SDL:
            func_text = "MEM_SDL";
            break;
        case StoreOpType::SDR:
            func_text = "MEM_SDR";
            break;
        case StoreOpType::SW:
            func_text = "MEM_SW";
            break;
        case StoreOpType::SWL:
            func_text = "MEM_SWL";
            break;
        case StoreOpType::SWR:
            func_text = "MEM_SWR";
            break;
        case StoreOpType::SH:
            func_text = "MEM_SH";
            break;
        case StoreOpType::SB:
            func_text = "MEM_SB";
            break;
        case StoreOpType::SDC1:
            func_text = "MEM_SD";
            break;
        case StoreOpType::SWC1:
            func_text = "MEM_SW";
            break;
        default:
            throw std::runtime_error("Unhandled store op");
    }

    fmt::print(output_file, "{}(rdram, {}, {}, {});\n", func_text, imm_str, base_str, value_input);
}

void N64Recomp::LuajitGenerator::emit_function_start(const std::string& function_name) const {
    fmt::print(output_file,
        "function {}(rdram, ctx)\n"
        // these variables shouldn't need to be preserved across function boundaries, so make them local for more efficient output
        "    local hi = 0ULL\n"
        "    local lo = 0ULL\n"
        "    local result = 0ULL\n"
        "    local rounding_mode = DEFAULT_ROUNDING_MODE\n"
        "    local c1cs = 0ULL\n", // cop1 conditional signal
        function_name);
}

void N64Recomp::LuajitGenerator::emit_function_end() const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_lookup(uint32_t addr) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_by_register(int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_by_name(const std::string& func_name) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_goto(const std::string& target) const {
    fmt::print(output_file, "goto {}\n", target);
}

void N64Recomp::LuajitGenerator::emit_label(const std::string& label_name) const {
    fmt::print(output_file, "::{}::\n", label_name);
}

void N64Recomp::LuajitGenerator::emit_variable_declaration(const std::string& var_name, int reg) const {
    // TODO
    fmt::print(output_file, "{} = 0\n", var_name);
}

void N64Recomp::LuajitGenerator::emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // Thread local variables to prevent allocations when possible.
    // TODO these thread locals probably don't actually help right now, so figure out a better way to prevent allocations.
    thread_local std::string expr_string{};
    get_binary_expr_string(op.comparison, op.operands, ctx, "", expr_string);
    fmt::print(output_file, "if {} then\n", expr_string);
}

void N64Recomp::LuajitGenerator::emit_branch_close() const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_switch(const std::string& jump_variable, int shift_amount) const {
    fmt::print(output_file, "do local case_index = bit.rshift({}, {}ULL)\n", jump_variable, shift_amount);
}

void N64Recomp::LuajitGenerator::emit_case(int case_index, const std::string& target_label) const {
    fmt::print(output_file, "if case_index == {} then goto {} end\n", case_index, target_label);
}

void N64Recomp::LuajitGenerator::emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const {
    fmt::print(output_file, "switch_error(\'lua\', {:08X}, {:08X})\n", instr_vram, jtbl_vram);
}

void N64Recomp::LuajitGenerator::emit_switch_close() const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_return() const {
    // Wrap the retur in a do/end construct to prevent errors from statements after the return.
    fmt::print(output_file, "do return end\n");
}

void N64Recomp::LuajitGenerator::emit_check_fr(int fpr) const {
    // Not used
}

void N64Recomp::LuajitGenerator::emit_check_nan(int fpr, bool is_double) const {
    // Not used
}

void N64Recomp::LuajitGenerator::emit_cop0_status_read(int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop0_status_write(int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop1_cs_read(int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop1_cs_write(int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_muldiv(InstrId instr_id, int reg1, int reg2) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_syscall(uint32_t instr_vram) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_do_break(uint32_t instr_vram) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_pause_self() const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_trigger_event(size_t event_index) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_comment(const std::string& comment) const {
    fmt::print(output_file, "-- {}\n", comment);
}
