#include <cassert>
#include <fstream>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "generator.h"

void N64Recomp::LuajitGenerator::process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_function_start(std::ostream& output_file, const std::string& function_name) const {
    fmt::print(output_file, "function {}(rdram, ctx)\n", function_name);
}

void N64Recomp::LuajitGenerator::emit_function_end(std::ostream& output_file) const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_lookup(std::ostream& output_file, uint32_t addr) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_by_register(std::ostream& output_file, int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_function_call_by_name(std::ostream& output_file, const std::string& func_name) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_goto(std::ostream& output_file, const std::string& target) const {
    fmt::print(output_file, "goto {}\n", target);
}

void N64Recomp::LuajitGenerator::emit_label(std::ostream& output_file, const std::string& label_name) const {
    fmt::print(output_file, "::{}::\n", label_name);
}

void N64Recomp::LuajitGenerator::emit_variable_declaration(std::ostream& output_file, const std::string& var_name, int reg) const {
    // TODO
    fmt::print(output_file, "{} = 0\n", var_name);
}

void N64Recomp::LuajitGenerator::emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // TODO
    fmt::print(output_file, "if (true) then\n");
}

void N64Recomp::LuajitGenerator::emit_branch_close(std::ostream& output_file) const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_switch(std::ostream& output_file, const std::string& jump_variable, int shift_amount) const {
    fmt::print(output_file, "do local case_index = bit.rshift({}, {}ULL)\n", jump_variable, shift_amount);
}

void N64Recomp::LuajitGenerator::emit_case(std::ostream& output_file, int case_index, const std::string& target_label) const {
    fmt::print(output_file, "if case_index == {} then goto {} end\n", case_index, target_label);
}

void N64Recomp::LuajitGenerator::emit_switch_error(std::ostream& output_file, uint32_t instr_vram, uint32_t jtbl_vram) const {
    fmt::print(output_file, "switch_error(\'lua\', {:08X}, {:08X})\n", instr_vram, jtbl_vram);
}

void N64Recomp::LuajitGenerator::emit_switch_close(std::ostream& output_file) const {
    fmt::print(output_file, "end\n");
}

void N64Recomp::LuajitGenerator::emit_return(std::ostream& output_file) const {
    fmt::print(output_file, "return\n");
}

void N64Recomp::LuajitGenerator::emit_check_fr(std::ostream& output_file, int fpr) const {
    // Not used
}

void N64Recomp::LuajitGenerator::emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const {
    // Not used
}

void N64Recomp::LuajitGenerator::emit_cop0_status_read(std::ostream& output_file, int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop0_status_write(std::ostream& output_file, int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop1_cs_read(std::ostream& output_file, int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_cop1_cs_write(std::ostream& output_file, int reg) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_muldiv(std::ostream& output_file, InstrId instr_id, int reg1, int reg2) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_syscall(std::ostream& output_file, uint32_t instr_vram) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_do_break(std::ostream& output_file, uint32_t instr_vram) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_pause_self(std::ostream& output_file) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_trigger_event(std::ostream& output_file, size_t event_index) const {
    // TODO
    fmt::print(output_file, "\n");
}

void N64Recomp::LuajitGenerator::emit_comment(std::ostream& output_file, const std::string& comment) const {
    fmt::print(output_file, "-- {}\n", comment);
}
