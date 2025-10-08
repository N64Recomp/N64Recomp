#include <cassert>
#include <fstream>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/lua_generator.h"
#include "recompiler/context.h"

struct LuaBinaryOpFields { std::string func_string; std::string infix_string; };

static std::vector<LuaBinaryOpFields> lua_op_fields = []() {
    std::vector<LuaBinaryOpFields> ret{};
    ret.resize(static_cast<size_t>(N64Recomp::BinaryOpType::COUNT));
    std::vector<char> ops_setup{};
    ops_setup.resize(static_cast<size_t>(N64Recomp::BinaryOpType::COUNT));

    auto setup_op = [&ret, &ops_setup](N64Recomp::BinaryOpType op_type, const std::string& func_string, const std::string& infix_string) {
        size_t index = static_cast<size_t>(op_type);
        assert(ops_setup[index] == false && "Operation already setup!");
        ops_setup[index] = true;
        ret[index] = { func_string, infix_string };
    };

    // Arithmetic operations
    setup_op(N64Recomp::BinaryOpType::Add32,     "add32",  "");
    setup_op(N64Recomp::BinaryOpType::Sub32,     "sub32",  "");
    setup_op(N64Recomp::BinaryOpType::Add64,     "",       "+");
    setup_op(N64Recomp::BinaryOpType::Sub64,     "",       "-");
    setup_op(N64Recomp::BinaryOpType::And64,     "band",   "");
    setup_op(N64Recomp::BinaryOpType::AddFloat,  "",       "+");
    setup_op(N64Recomp::BinaryOpType::AddDouble, "",       "+");
    setup_op(N64Recomp::BinaryOpType::SubFloat,  "",       "-");
    setup_op(N64Recomp::BinaryOpType::SubDouble, "",       "-");
    setup_op(N64Recomp::BinaryOpType::MulFloat,  "",       "*");
    setup_op(N64Recomp::BinaryOpType::MulDouble, "",       "*");
    setup_op(N64Recomp::BinaryOpType::DivFloat,  "",       "/");
    setup_op(N64Recomp::BinaryOpType::DivDouble, "",       "/");
    setup_op(N64Recomp::BinaryOpType::Or64,      "bor",    "");
    setup_op(N64Recomp::BinaryOpType::Nor64,     "bnot",   "");
    setup_op(N64Recomp::BinaryOpType::Xor64,     "bxor",   "");
    setup_op(N64Recomp::BinaryOpType::Sll32,     "sll32",  "");
    setup_op(N64Recomp::BinaryOpType::Sll64,     "lshift", "");
    setup_op(N64Recomp::BinaryOpType::Srl32,     "srl32",  "");
    setup_op(N64Recomp::BinaryOpType::Srl64,     "rshift", "");
    setup_op(N64Recomp::BinaryOpType::Sra32,     "sra32",  "");
    setup_op(N64Recomp::BinaryOpType::Sra64,     "arshift","");
    // Comparisons
    setup_op(N64Recomp::BinaryOpType::Equal,     "",       "==");
    setup_op(N64Recomp::BinaryOpType::EqualFloat,"",       "==");
    setup_op(N64Recomp::BinaryOpType::EqualDouble,"",      "==");
    setup_op(N64Recomp::BinaryOpType::NotEqual,  "",       "~=");
    setup_op(N64Recomp::BinaryOpType::Less,      "",       "<");
    setup_op(N64Recomp::BinaryOpType::LessFloat, "",       "<");
    setup_op(N64Recomp::BinaryOpType::LessDouble,"",       "<");
    setup_op(N64Recomp::BinaryOpType::LessEq,    "",       "<=");
    setup_op(N64Recomp::BinaryOpType::LessEqFloat,"",      "<=");
    setup_op(N64Recomp::BinaryOpType::LessEqDouble,"",     "<=");
    setup_op(N64Recomp::BinaryOpType::Greater,   "",       ">");
    setup_op(N64Recomp::BinaryOpType::GreaterEq, "",       ">=");
    // Loads
    setup_op(N64Recomp::BinaryOpType::LD,        "ld",     "");
    setup_op(N64Recomp::BinaryOpType::LW,        "lw",     "");
    setup_op(N64Recomp::BinaryOpType::LWU,       "lwu",    "");
    setup_op(N64Recomp::BinaryOpType::LH,        "lh",     "");
    setup_op(N64Recomp::BinaryOpType::LHU,       "lhu",    "");
    setup_op(N64Recomp::BinaryOpType::LB,        "lb",     "");
    setup_op(N64Recomp::BinaryOpType::LBU,       "lbu",    "");
    setup_op(N64Recomp::BinaryOpType::LDL,       "ldl",    "");
    setup_op(N64Recomp::BinaryOpType::LDR,       "ldr",    "");
    setup_op(N64Recomp::BinaryOpType::LWL,       "lwl",    "");
    setup_op(N64Recomp::BinaryOpType::LWR,       "lwr",    "");
    setup_op(N64Recomp::BinaryOpType::True,      "", "");
    setup_op(N64Recomp::BinaryOpType::False,     "", "");

    for (char is_set : ops_setup) {
        assert(is_set && "Operation has not been setup!");
    }

    return ret;
}();

static std::string lua_gpr_to_string(int gpr_index) {
    if (gpr_index == 0) {
        return "0";
    }
    return fmt::format("ctx.r{}", gpr_index);
}

static std::string lua_fpr_to_string(int fpr_index) {
    return fmt::format("ctx.f{}", fpr_index);
}

static std::string lua_fpr_double_to_string(int fpr_index) {
    return fmt::format("ctx.f{}_d", fpr_index);
}

static std::string lua_fpr_u32l_to_string(int fpr_index) {
    if (fpr_index & 1) {
        return fmt::format("ctx.f_odd[{}]", (fpr_index - 1) / 2);
    }
    return fmt::format("ctx.f{}_l", fpr_index);
}

static std::string lua_fpr_u64_to_string(int fpr_index) {
    return fmt::format("ctx.f{}_u64", fpr_index);
}

void N64Recomp::LuaGenerator::get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const {
    switch (operand) {
        case Operand::Rd:
            operand_string = lua_gpr_to_string(context.rd);
            break;
        case Operand::Rs:
            operand_string = lua_gpr_to_string(context.rs);
            break;
        case Operand::Rt:
            operand_string = lua_gpr_to_string(context.rt);
            break;
        case Operand::Fd:
            operand_string = lua_fpr_to_string(context.fd);
            break;
        case Operand::Fs:
            operand_string = lua_fpr_to_string(context.fs);
            break;
        case Operand::Ft:
            operand_string = lua_fpr_to_string(context.ft);
            break;
        case Operand::FdDouble:
            operand_string = lua_fpr_double_to_string(context.fd);
            break;
        case Operand::FsDouble:
            operand_string = lua_fpr_double_to_string(context.fs);
            break;
        case Operand::FtDouble:
            operand_string = lua_fpr_double_to_string(context.ft);
            break;
        case Operand::FdU32L:
            operand_string = lua_fpr_u32l_to_string(context.fd);
            break;
        case Operand::FsU32L:
            operand_string = lua_fpr_u32l_to_string(context.fs);
            break;
        case Operand::FtU32L:
            operand_string = lua_fpr_u32l_to_string(context.ft);
            break;
        case Operand::FdU64:
            operand_string = lua_fpr_u64_to_string(context.fd);
            break;
        case Operand::FsU64:
            operand_string = lua_fpr_u64_to_string(context.fs);
            break;
        case Operand::FtU64:
            operand_string = lua_fpr_u64_to_string(context.ft);
            break;
        case Operand::ImmU16:
            operand_string = fmt::format("0x{:X}", context.imm16);
            break;
        case Operand::ImmS16:
            operand_string = fmt::format("{}", (int16_t)context.imm16);
            break;
        case Operand::Sa:
            operand_string = std::to_string(context.sa);
            break;
        case Operand::Sa32:
            operand_string = fmt::format("{}", context.sa + 32);
            break;
        case Operand::Cop1cs:
            operand_string = "ctx.cop1_cs";
            break;
        case Operand::Hi:
            operand_string = "ctx.hi";
            break;
        case Operand::Lo:
            operand_string = "ctx.lo";
            break;
        case Operand::Zero:
            operand_string = "0";
            break;
    }

    // Apply unary operations
    switch (operation) {
        case UnaryOpType::None:
            break;
        case UnaryOpType::ToS32:
            operand_string = fmt::format("s32({})", operand_string);
            break;
        case UnaryOpType::ToU32:
            operand_string = fmt::format("u32({})", operand_string);
            break;
        case UnaryOpType::ToS64:
            operand_string = fmt::format("s64({})", operand_string);
            break;
        case UnaryOpType::ToU64:
            operand_string = fmt::format("u64({})", operand_string);
            break;
        case UnaryOpType::Lui:
            operand_string = fmt::format("s32(lshift({}, 16))", operand_string);
            break;
        case UnaryOpType::Mask5:
            operand_string = fmt::format("band({}, 31)", operand_string);
            break;
        case UnaryOpType::Mask6:
            operand_string = fmt::format("band({}, 63)", operand_string);
            break;
        case UnaryOpType::NegateFloat:
        case UnaryOpType::NegateDouble:
            operand_string = fmt::format("-({})", operand_string);
            break;
        case UnaryOpType::AbsFloat:
        case UnaryOpType::AbsDouble:
            operand_string = fmt::format("math.abs({})", operand_string);
            break;
        case UnaryOpType::SqrtFloat:
        case UnaryOpType::SqrtDouble:
            operand_string = fmt::format("math.sqrt({})", operand_string);
            break;
        case UnaryOpType::ConvertSFromW:
        case UnaryOpType::ConvertDFromW:
        case UnaryOpType::ConvertDFromS:
        case UnaryOpType::ConvertSFromD:
        case UnaryOpType::ConvertDFromL:
        case UnaryOpType::ConvertSFromL:
            operand_string = fmt::format("tonumber({})", operand_string);
            break;
        case UnaryOpType::ConvertWFromS:
        case UnaryOpType::ConvertWFromD:
        case UnaryOpType::TruncateWFromS:
        case UnaryOpType::TruncateWFromD:
            operand_string = fmt::format("math.floor({})", operand_string);
            break;
        case UnaryOpType::ConvertLFromD:
        case UnaryOpType::ConvertLFromS:
        case UnaryOpType::TruncateLFromS:
        case UnaryOpType::TruncateLFromD:
            operand_string = fmt::format("math.floor({})", operand_string);
            break;
        case UnaryOpType::RoundWFromS:
        case UnaryOpType::RoundWFromD:
        case UnaryOpType::RoundLFromS:
        case UnaryOpType::RoundLFromD:
            operand_string = fmt::format("math.floor({} + 0.5)", operand_string);
            break;
        case UnaryOpType::CeilWFromS:
        case UnaryOpType::CeilWFromD:
        case UnaryOpType::CeilLFromS:
        case UnaryOpType::CeilLFromD:
            operand_string = fmt::format("math.ceil({})", operand_string);
            break;
        case UnaryOpType::FloorWFromS:
        case UnaryOpType::FloorWFromD:
        case UnaryOpType::FloorLFromS:
        case UnaryOpType::FloorLFromD:
            operand_string = fmt::format("math.floor({})", operand_string);
            break;
        case UnaryOpType::ToInt32:
            operand_string = fmt::format("s32({})", operand_string);
            break;
    }
}

void N64Recomp::LuaGenerator::get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const {
    func_string = lua_op_fields[static_cast<size_t>(op_type)].func_string;
    infix_string = lua_op_fields[static_cast<size_t>(op_type)].infix_string;
}

void N64Recomp::LuaGenerator::get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const {
    thread_local std::string input_a{};
    thread_local std::string input_b{};
    thread_local std::string func_string{};
    thread_local std::string infix_string{};
    
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
        expr_string = fmt::format("({} {} {})", input_a, infix_string, input_b);
    }
    else {
        if (type == BinaryOpType::True) {
            expr_string = "true";
        }
        else if (type == BinaryOpType::False) {
            expr_string = "false";
        }
    }
}

void N64Recomp::LuaGenerator::emit_comment(const std::string& comment) const {
    fmt::print(output_file, "    -- {}\n", comment);
}

void N64Recomp::LuaGenerator::process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const {
    thread_local std::string output{};
    thread_local std::string expression{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_binary_expr_string(op.type, op.operands, ctx, output, expression);
    fmt::print(output_file, "    {} = {}\n", output, expression);
}

void N64Recomp::LuaGenerator::process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const {
    thread_local std::string output{};
    thread_local std::string input{};
    get_operand_string(op.output, UnaryOpType::None, ctx, output);
    get_operand_string(op.input, op.operation, ctx, input);
    fmt::print(output_file, "    {} = {}\n", output, input);
}

void N64Recomp::LuaGenerator::process_store_op(const StoreOp& op, const InstructionContext& ctx) const {
    thread_local std::string base_str{};
    thread_local std::string imm_str{};
    thread_local std::string value_input{};
    get_operand_string(Operand::Base, UnaryOpType::None, ctx, base_str);
    get_operand_string(Operand::ImmS16, UnaryOpType::None, ctx, imm_str);
    get_operand_string(op.value_input, UnaryOpType::None, ctx, value_input);

    std::string store_func;
    switch (op.type) {
        case StoreOpType::SD:
            store_func = "sd";
            break;
        case StoreOpType::SDL:
            store_func = "sdl";
            break;
        case StoreOpType::SDR:
            store_func = "sdr";
            break;
        case StoreOpType::SW:
            store_func = "sw";
            break;
        case StoreOpType::SWL:
            store_func = "swl";
            break;
        case StoreOpType::SWR:
            store_func = "swr";
            break;
        case StoreOpType::SH:
            store_func = "sh";
            break;
        case StoreOpType::SB:
            store_func = "sb";
            break;
        case StoreOpType::SDC1:
            store_func = "sdc1";
            break;
        case StoreOpType::SWC1:
            store_func = "swc1";
            break;
        default:
            throw std::runtime_error("Unhandled store op");
    }

    fmt::print(output_file, "    {}(rdram, {}, {}, {})\n", store_func, value_input, imm_str, base_str);
}

void N64Recomp::LuaGenerator::emit_function_start(const std::string& function_name, size_t func_index) const {
    fmt::print(output_file, "function {}(rdram, ctx)\n", function_name);
}

void N64Recomp::LuaGenerator::emit_function_end() const {
    fmt::print(output_file, "end\n\n");
}

void N64Recomp::LuaGenerator::emit_function_call_lookup(uint32_t addr) const {
    fmt::print(output_file, "    lookup_func(0x{:08X})(rdram, ctx)\n", addr);
}

void N64Recomp::LuaGenerator::emit_function_call_by_register(int reg) const {
    fmt::print(output_file, "    lookup_func({})(rdram, ctx)\n", lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const {
    const N64Recomp::ReferenceSymbol& sym = context.get_reference_symbol(section_index, symbol_index);
    fmt::print(output_file, "    {}(rdram, ctx)\n", sym.name);
}

void N64Recomp::LuaGenerator::emit_function_call(const Context& context, size_t function_index) const {
    fmt::print(output_file, "    {}(rdram, ctx)\n", context.functions[function_index].name);
}

void N64Recomp::LuaGenerator::emit_named_function_call(const std::string& function_name) const {
    fmt::print(output_file, "    {}(rdram, ctx)\n", function_name);
}

void N64Recomp::LuaGenerator::emit_goto(const std::string& target) const {
    fmt::print(output_file, "    goto {}\n", target);
}

void N64Recomp::LuaGenerator::emit_label(const std::string& label_name) const {
    fmt::print(output_file, "::{}: :\n", label_name);
}

void N64Recomp::LuaGenerator::emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const {
    std::string jump_variable = fmt::format("jr_addend_{:08X}", jtbl.jr_vram);
    fmt::print(output_file, "    local {} = {}\n", jump_variable, lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    thread_local std::string expr_string{};
    get_binary_expr_string(op.comparison, op.operands, ctx, "", expr_string);
    fmt::print(output_file, "    if {} then\n", expr_string);
}

void N64Recomp::LuaGenerator::emit_branch_close() const {
    fmt::print(output_file, "    end\n");
}

void N64Recomp::LuaGenerator::emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const {
    std::string jump_variable = fmt::format("jr_addend_{:08X}", jtbl.jr_vram);
    fmt::print(output_file, "    local switch_val = rshift({}, 2)\n", jump_variable);
}

void N64Recomp::LuaGenerator::emit_case(int case_index, const std::string& target_label) const {
    if (case_index == 0) {
        fmt::print(output_file, "    if switch_val == {} then goto {} ", case_index, target_label);
    } else {
        fmt::print(output_file, "elseif switch_val == {} then goto {} ", case_index, target_label);
    }
}

void N64Recomp::LuaGenerator::emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const {
    fmt::print(output_file, "else error(string.format('Invalid switch value at 0x{:08X}')) end\n", instr_vram);
}

void N64Recomp::LuaGenerator::emit_switch_close() const {
    // No closing needed for Lua switch
}

void N64Recomp::LuaGenerator::emit_return(const Context& context, size_t func_index) const {
    fmt::print(output_file, "    return\n");
}

void N64Recomp::LuaGenerator::emit_check_fr(int fpr) const {
    fmt::print(output_file, "    check_fr(ctx, {})\n", fpr);
}

void N64Recomp::LuaGenerator::emit_check_nan(int fpr, bool is_double) const {
    fmt::print(output_file, "    -- NAN check for f{}\n", fpr);
}

void N64Recomp::LuaGenerator::emit_cop0_status_read(int reg) const {
    fmt::print(output_file, "    {} = cop0_status_read(ctx)\n", lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_cop0_status_write(int reg) const {
    fmt::print(output_file, "    cop0_status_write(ctx, {})\n", lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_cop1_cs_read(int reg) const {
    fmt::print(output_file, "    {} = get_cop1_cs()\n", lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_cop1_cs_write(int reg) const {
    fmt::print(output_file, "    set_cop1_cs({})\n", lua_gpr_to_string(reg));
}

void N64Recomp::LuaGenerator::emit_muldiv(InstrId instr_id, int reg1, int reg2) const {
    switch (instr_id) {
        case InstrId::cpu_mult:
            fmt::print(output_file, "    ctx.lo, ctx.hi = mult({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_multu:
            fmt::print(output_file, "    ctx.lo, ctx.hi = multu({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_div:
            fmt::print(output_file, "    ctx.lo, ctx.hi = div({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_divu:
            fmt::print(output_file, "    ctx.lo, ctx.hi = divu({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_dmult:
            fmt::print(output_file, "    ctx.lo, ctx.hi = dmult({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_dmultu:
            fmt::print(output_file, "    ctx.lo, ctx.hi = dmultu({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_ddiv:
            fmt::print(output_file, "    ctx.lo, ctx.hi = ddiv({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        case InstrId::cpu_ddivu:
            fmt::print(output_file, "    ctx.lo, ctx.hi = ddivu({}, {})\n", 
                lua_gpr_to_string(reg1), lua_gpr_to_string(reg2));
            break;
        default:
            assert(false);
            break;
    }
}

void N64Recomp::LuaGenerator::emit_syscall(uint32_t instr_vram) const {
    fmt::print(output_file, "    syscall_handler(rdram, ctx, 0x{:08X})\n", instr_vram);
}

void N64Recomp::LuaGenerator::emit_do_break(uint32_t instr_vram) const {
    fmt::print(output_file, "    do_break(0x{:08X})\n", instr_vram);
}

void N64Recomp::LuaGenerator::emit_pause_self() const {
    fmt::print(output_file, "    pause_self(rdram)\n");
}

void N64Recomp::LuaGenerator::emit_trigger_event(uint32_t event_index) const {
    fmt::print(output_file, "    trigger_event(rdram, ctx, {})\n", event_index);
}

bool N64Recomp::recompile_function_lua(const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    LuaGenerator generator{output_file};
    return recompile_function_custom(generator, context, function_index, output_file, static_funcs_out, tag_reference_relocs);
}