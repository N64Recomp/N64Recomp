#include <optional>
#include <fstream>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cassert>
#include <filesystem>
#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"
#include <iostream>

using InstrId = rabbitizer::InstrId::UniqueId;
using Cop0Reg = rabbitizer::Registers::Rsp::Cop0;
constexpr size_t instr_size = sizeof(uint32_t);
constexpr uint32_t rsp_mem_mask = 0x1FFF;

// Can't use rabbitizer's operand types because we need to be able to provide a register reference or a register index
enum class RspOperand {
    None,
    Vt,
    VtIndex,
    Vd,
    Vs,
    VsIndex,
    De,
    Rt,
    Rs,
    Imm7,
};

std::unordered_map<InstrId, std::array<RspOperand, 3>> vector_operands{
    // Vt, Rs, Imm
    { InstrId::rsp_lbv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_ldv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lfv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lhv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_llv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lpv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lqv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lrv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_lsv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_luv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    // { InstrId::rsp_lwv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}}, // Not in rabbitizer
    { InstrId::rsp_sbv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_sdv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_sfv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_shv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_slv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_spv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_sqv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_srv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_ssv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_suv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_swv, {RspOperand::Vt, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_stv, {RspOperand::VtIndex, RspOperand::Rs, RspOperand::Imm7}},
    { InstrId::rsp_ltv, {RspOperand::VtIndex, RspOperand::Rs, RspOperand::Imm7}},

    // Vd, Vs, Vt
    { InstrId::rsp_vabs,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vadd,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vaddc,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vand,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vch,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vcl,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vcr,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_veq,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vge,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vlt,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmacf,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmacu,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmadh,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmadl,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmadm,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmadn,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmrg,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmudh,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmudl,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmudm,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmudn,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vne,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vnor,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vnxor,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vor,     {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vsub,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vsubc,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmulf,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmulu,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vmulq,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vnand,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vxor,    {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}},
    { InstrId::rsp_vsar,    {RspOperand::Vd, RspOperand::Vs, RspOperand::None}},
    { InstrId::rsp_vmacq,   {RspOperand::Vd, RspOperand::None, RspOperand::None}},
    // { InstrId::rsp_vzero,   {RspOperand::Vd, RspOperand::Vs, RspOperand::Vt}}, unused pseudo
    { InstrId::rsp_vrndn,   {RspOperand::Vd, RspOperand::VsIndex, RspOperand::Vt}},
    { InstrId::rsp_vrndp,   {RspOperand::Vd, RspOperand::VsIndex, RspOperand::Vt}},

    // Vd, De, Vt
    { InstrId::rsp_vmov,    {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrcp,    {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrcpl,   {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrcph,   {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrsq,    {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrsql,   {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},
    { InstrId::rsp_vrsqh,   {RspOperand::Vd, RspOperand::De, RspOperand::Vt}},

    // Rt, Vs
    { InstrId::rsp_mfc2,    {RspOperand::Rt, RspOperand::Vs, RspOperand::None}},
    { InstrId::rsp_mtc2,    {RspOperand::Rt, RspOperand::Vs, RspOperand::None}},

    // Nop
    { InstrId::rsp_vnop,    {RspOperand::None, RspOperand::None, RspOperand::None}}
};

std::string_view ctx_gpr_prefix(int reg) {
    if (reg != 0) {
        return "r";
    }
    return "";
}

uint32_t expected_c0_reg_value(int cop0_reg) {
    switch (static_cast<Cop0Reg>(cop0_reg)) {
    case Cop0Reg::RSP_COP0_SP_STATUS:
        return 0; // None of the flags in RSP status are set
    case Cop0Reg::RSP_COP0_SP_DMA_FULL:
        return 0; // Pretend DMAs complete instantly
    case Cop0Reg::RSP_COP0_SP_DMA_BUSY:
        return 0; // Pretend DMAs complete instantly
    case Cop0Reg::RSP_COP0_SP_SEMAPHORE:
        return 0; // Always acquire the semaphore
    case Cop0Reg::RSP_COP0_DPC_STATUS:
        return 0; // Good enough for the microcodes that would be recompiled (i.e. non-graphics ones)
    default:
        fmt::print(stderr, "Unhandled mfc0: {}\n", cop0_reg);
        throw std::runtime_error("Unhandled mfc0");
        return 0;
    }
}

std::string_view c0_reg_write_action(int cop0_reg) {
    switch (static_cast<Cop0Reg>(cop0_reg)) {
    case Cop0Reg::RSP_COP0_SP_SEMAPHORE:
        return ""; // Ignore semaphore functionality
    case Cop0Reg::RSP_COP0_SP_STATUS:
        return ""; // Ignore writes to the status flags since yielding is ignored
    case Cop0Reg::RSP_COP0_SP_DRAM_ADDR:
        return "SET_DMA_DRAM";
    case Cop0Reg::RSP_COP0_SP_MEM_ADDR:
        return "SET_DMA_DMEM";
    case Cop0Reg::RSP_COP0_SP_RD_LEN:
        return "DO_DMA_READ";
    case Cop0Reg::RSP_COP0_SP_WR_LEN:
        return "DO_DMA_WRITE";
    default:
        fmt::print(stderr, "Unhandled mtc0: {}\n", cop0_reg);
        throw std::runtime_error("Unhandled mtc0");
    }

}

std::optional<int> get_rsp_element(const rabbitizer::InstructionRsp& instr) {
    if (instr.hasOperand(rabbitizer::OperandType::rsp_vt_elementhigh)) {
        return instr.GetRsp_elementhigh();
    } else if (instr.hasOperand(rabbitizer::OperandType::rsp_vt_elementlow) || instr.hasOperand(rabbitizer::OperandType::rsp_vs_index)) {
        return instr.GetRsp_elementlow();
    }

    return std::nullopt;
}

bool rsp_ignores_element(InstrId id) {
    return id == InstrId::rsp_vmacq || id == InstrId::rsp_vnop;
}

struct BranchTargets {
    std::unordered_set<uint32_t> direct_targets;
    std::unordered_set<uint32_t> indirect_targets;
};

BranchTargets get_branch_targets(const std::vector<rabbitizer::InstructionRsp>& instrs) {
    BranchTargets ret;
    for (const auto& instr : instrs) {
        if (instr.isJumpWithAddress() || instr.isBranch()) {
            ret.direct_targets.insert(instr.getBranchVramGeneric() & rsp_mem_mask);
        }
        if (instr.doesLink()) {
            ret.indirect_targets.insert(instr.getVram() + 2 * instr_size);
        }
    }
    return ret;
}

bool process_instruction(size_t instr_index, const std::vector<rabbitizer::InstructionRsp>& instructions, std::ofstream& output_file, const BranchTargets& branch_targets, const std::unordered_set<uint32_t>& unsupported_instructions, bool indent, bool in_delay_slot) {
    const auto& instr = instructions[instr_index];

    uint32_t instr_vram = instr.getVram();
    InstrId instr_id = instr.getUniqueId();

    // Skip labels if we're duplicating an instruction into a delay slot
    if (!in_delay_slot) {
        // Print a label if one exists here
        if (branch_targets.direct_targets.contains(instr_vram) || branch_targets.indirect_targets.contains(instr_vram)) {
            fmt::print(output_file, "L_{:04X}:\n", instr_vram);
        }
    }

    uint16_t branch_target = instr.getBranchVramGeneric() & rsp_mem_mask;

    // Output a comment with the original instruction
    if (instr.isBranch() || instr_id == InstrId::rsp_j) {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0, fmt::format("L_{:04X}", branch_target)));
    } else if (instr_id == InstrId::rsp_jal) {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0, fmt::format("0x{:04X}", branch_target)));
    } else {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0));
    }

    auto print_indent = [&]() {
        fmt::print(output_file, "    ");
    };

    auto print_line = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts&& ...args) {
        print_indent();
        fmt::print(output_file, fmt_str, std::forward<Ts>(args)...);
        fmt::print(output_file, ";\n");
    };

    auto print_branch_condition = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts&& ...args) {
        fmt::print(output_file, fmt_str, std::forward<Ts>(args)...);
        fmt::print(output_file, " ");
    };

    auto print_unconditional_branch = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts&& ...args) {
        if (instr_index < instructions.size() - 1) {
            uint32_t next_vram = instr_vram + 4;
            process_instruction(instr_index + 1, instructions, output_file, branch_targets, unsupported_instructions, false, true);
        }
        print_indent();
        fmt::print(output_file, fmt_str, std::forward<Ts>(args)...);
        fmt::print(output_file, ";\n");
    };

    auto print_branch = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts&& ...args) {
        fmt::print(output_file, "{{\n    ");
        if (instr_index < instructions.size() - 1) {
            uint32_t next_vram = instr_vram + 4;
            process_instruction(instr_index + 1, instructions, output_file, branch_targets, unsupported_instructions, true, true);
        }
        fmt::print(output_file, "        ");
        fmt::print(output_file, fmt_str, std::forward<Ts>(args)...);
        fmt::print(output_file, ";\n    }}\n");
    };

    if (indent) {
        print_indent();
    }

    // Replace unsupported instructions with early returns
    if (unsupported_instructions.contains(instr_vram)) {
        print_line("return RspExitReason::Unsupported", instr_vram);
        if (indent) {
            print_indent();
        }
    }

    int rd = (int)instr.GetO32_rd();
    int rs = (int)instr.GetO32_rs();
    int base = rs;
    int rt = (int)instr.GetO32_rt();
    int sa = (int)instr.Get_sa();

    int fd = (int)instr.GetO32_fd();
    int fs = (int)instr.GetO32_fs();
    int ft = (int)instr.GetO32_ft();

    uint16_t imm = instr.Get_immediate();

    std::string unsigned_imm_string = fmt::format("{:#X}", imm);
    std::string signed_imm_string = fmt::format("{:#X}", (int16_t)imm);

    auto rsp_element = get_rsp_element(instr);

    // If this instruction is in the vector operand table then emit the appropriate function call for its implementation
    auto operand_find_it = vector_operands.find(instr_id);
    if (operand_find_it != vector_operands.end()) {
        const auto& operands = operand_find_it->second;
        int vd = (int)instr.GetRsp_vd();
        int vs = (int)instr.GetRsp_vs();
        int vt = (int)instr.GetRsp_vt();
        std::string operand_string = "";
        for (RspOperand operand : operands) {
            switch (operand) {
                case RspOperand::Vt:
                    operand_string += fmt::format("rsp.vpu.r[{}], ", vt);
                    break;
                case RspOperand::VtIndex:
                    operand_string += fmt::format("{}, ", vt);
                    break;
                case RspOperand::Vd:
                    operand_string += fmt::format("rsp.vpu.r[{}], ", vd);
                    break;
                case RspOperand::Vs:
                    operand_string += fmt::format("rsp.vpu.r[{}], ", vs);
                    break;
                case RspOperand::VsIndex:
                    operand_string += fmt::format("{}, ", vs);
                    break;
                case RspOperand::De:
                    operand_string += fmt::format("{}, ", instr.GetRsp_de());
                    break;
                case RspOperand::Rt:
                    operand_string += fmt::format("{}{}, ", ctx_gpr_prefix(rt), rt);
                    break;
                case RspOperand::Rs:
                    operand_string += fmt::format("{}{}, ", ctx_gpr_prefix(rs), rs);
                    break;
                case RspOperand::Imm7:
                    // Sign extend the 7-bit immediate
                    operand_string += fmt::format("{:#X}, ", ((int8_t)(imm << 1)) >> 1);
                    break;
                case RspOperand::None:
                    break;
            }
        }
        // Trim the trailing comma off the operands
        if (operand_string.size() > 0) {
            operand_string = operand_string.substr(0, operand_string.size() - 2);
        }
        std::string uppercase_name = "";
        std::string lowercase_name = instr.getOpcodeName();
        uppercase_name.reserve(lowercase_name.size() + 1);
        for (char c : lowercase_name) {
            uppercase_name += std::toupper(c);
        }
        if (rsp_ignores_element(instr_id)) {
            print_line("rsp.{}({})", uppercase_name, operand_string);
        } else {
            print_line("rsp.{}<{}>({})", uppercase_name, rsp_element.value(), operand_string);
        }
    }
    // Otherwise, implement the instruction directly
    else {
        switch (instr_id) {
        case InstrId::rsp_nop:
            fmt::print(output_file, "\n");
            break;
            // Arithmetic
        case InstrId::rsp_lui:
            print_line("{}{} = S32({} << 16)", ctx_gpr_prefix(rt), rt, unsigned_imm_string);
            break;
        case InstrId::rsp_add:
        case InstrId::rsp_addu:
            if (rd == 0) {
                fmt::print(output_file, "\n");
                break;
            }
            print_line("{}{} = RSP_ADD32({}{}, {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_negu: // pseudo instruction for subu x, 0, y
        case InstrId::rsp_sub:
        case InstrId::rsp_subu:
            print_line("{}{} = RSP_SUB32({}{}, {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_addi:
        case InstrId::rsp_addiu:
            print_line("{}{} = RSP_ADD32({}{}, {})", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
            break;
        case InstrId::rsp_and:
            if (rd == 0) {
                fmt::print(output_file, "\n");
                break;
            }
            print_line("{}{} = {}{} & {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_andi:
            print_line("{}{} = {}{} & {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
            break;
        case InstrId::rsp_or:
            print_line("{}{} = {}{} | {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_ori:
            print_line("{}{} = {}{} | {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
            break;
        case InstrId::rsp_nor:
            print_line("{}{} = ~({}{} | {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_xor:
            print_line("{}{} = {}{} ^ {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_xori:
            print_line("{}{} = {}{} ^ {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
            break;
        case InstrId::rsp_sll:
            print_line("{}{} = S32({}{}) << {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
            break;
        case InstrId::rsp_sllv:
            print_line("{}{} = S32({}{}) << ({}{} & 31)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
            break;
        case InstrId::rsp_sra:
            print_line("{}{} = S32(RSP_SIGNED({}{}) >> {})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
            break;
        case InstrId::rsp_srav:
            print_line("{}{} = S32(RSP_SIGNED({}{}) >> ({}{} & 31))", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
            break;
        case InstrId::rsp_srl:
            print_line("{}{} = S32(U32({}{}) >> {})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
            break;
        case InstrId::rsp_srlv:
            print_line("{}{} = S32(U32({}{}) >> ({}{} & 31))", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
            break;
        case InstrId::rsp_slt:
            print_line("{}{} = RSP_SIGNED({}{}) < RSP_SIGNED({}{}) ? 1 : 0", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_slti:
            print_line("{}{} = RSP_SIGNED({}{}) < {} ? 1 : 0", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
            break;
        case InstrId::rsp_sltu:
            print_line("{}{} = {}{} < {}{} ? 1 : 0", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_sltiu:
            print_line("{}{} = {}{} < {} ? 1 : 0", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
            break;
            // Loads
            // TODO ld
        case InstrId::rsp_lw:
            print_line("{}{} = RSP_MEM_W_LOAD({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
            break;
        case InstrId::rsp_lh:
            print_line("{}{} = RSP_MEM_H_LOAD({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
            break;
        case InstrId::rsp_lb:
            print_line("{}{} = RSP_MEM_B({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
            break;
        case InstrId::rsp_lhu:
            print_line("{}{} = RSP_MEM_HU_LOAD({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
            break;
        case InstrId::rsp_lbu:
            print_line("{}{} = RSP_MEM_BU({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
            break;
            // Stores
        case InstrId::rsp_sw:
            print_line("RSP_MEM_W_STORE({}, {}{}, {}{})", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_sh:
            print_line("RSP_MEM_H_STORE({}, {}{}, {}{})", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
            break;
        case InstrId::rsp_sb:
            print_line("RSP_MEM_B({}, {}{}) = {}{}", signed_imm_string, ctx_gpr_prefix(base), base, ctx_gpr_prefix(rt), rt);
            break;
            // Branches
        case InstrId::rsp_j:
        case InstrId::rsp_b:
            print_unconditional_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_jal:
            print_line("{}{} = 0x{:04X}", ctx_gpr_prefix(31), 31, instr_vram + 2 * instr_size);
            print_unconditional_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_jr:
            print_line("jump_target = {}{}", ctx_gpr_prefix(rs), rs);
            print_unconditional_branch("goto do_indirect_jump");
            break;
        case InstrId::rsp_jalr:
            print_line("jump_target = {}{}; {}{} = 0x{:8X}", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rd), rd, instr_vram + 2 * instr_size);
            print_unconditional_branch("goto do_indirect_jump");
            break;
        case InstrId::rsp_bne:
            print_indent();
            print_branch_condition("if ({}{} != {}{})", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_beq:
            print_indent();
            print_branch_condition("if ({}{} == {}{})", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_bgez:
            print_indent();
            print_branch_condition("if (RSP_SIGNED({}{}) >= 0)", ctx_gpr_prefix(rs), rs);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_bgtz:
            print_indent();
            print_branch_condition("if (RSP_SIGNED({}{}) > 0)", ctx_gpr_prefix(rs), rs);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_blez:
            print_indent();
            print_branch_condition("if (RSP_SIGNED({}{}) <= 0)", ctx_gpr_prefix(rs), rs);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_bltz:
            print_indent();
            print_branch_condition("if (RSP_SIGNED({}{}) < 0)", ctx_gpr_prefix(rs), rs);
            print_branch("goto L_{:04X}", branch_target);
            break;
        case InstrId::rsp_break:
            print_line("return RspExitReason::Broke", instr_vram);
            break;
        case InstrId::rsp_mfc0:
            print_line("{}{} = {}", ctx_gpr_prefix(rt), rt, expected_c0_reg_value(rd));
            break;
        case InstrId::rsp_mtc0:
            {
                std::string_view write_action = c0_reg_write_action(rd);
                if (!write_action.empty()) {
                    print_line("{}({}{})", write_action, ctx_gpr_prefix(rt), rt); \
                }
                break;
            }
        default:
            fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
            assert(false);
            return false;
        }
    }

    return true;
}

void write_indirect_jumps(std::ofstream& output_file, const BranchTargets& branch_targets, const std::string& output_function_name) {
    fmt::print(output_file,
        "do_indirect_jump:\n"
        "    switch ((jump_target | 0x1000) & {:#X}) {{ \n", rsp_mem_mask);
    for (uint32_t branch_target: branch_targets.indirect_targets) {
        fmt::print(output_file, "        case 0x{0:04X}: goto L_{0:04X};\n", branch_target);
    }
    fmt::print(output_file,
        "    }}\n"
        "    printf(\"Unhandled jump target 0x%04X in microcode {}\\n\", jump_target);\n"
        "    return RspExitReason::UnhandledJumpTarget;\n", output_function_name);
}

const std::unordered_set<uint32_t> unsupported_instructions{};

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

enum cli_args {
  eRSPTextOffset,
  eRSPTextSize,
  eRSPTextAddress,
  eRomFilePath,
  eOutputFilePath,
  eOutputFunctionName,
  eExtraIndirectBranchTargets,
  None
};

cli_args convert(const std::string& arg) {
  if (arg == "--rsp-text-offset") return eRSPTextOffset;
  if (arg == "--rsp-text-size") return eRSPTextSize;
  if (arg == "--rsp-text-address") return eRSPTextAddress;
  if (arg == "--rom-file-path") return eRomFilePath;
  if (arg == "--output-file-path") return eOutputFilePath;
  if (arg == "--output-function-name") return eOutputFunctionName;
  if (arg == "--extra-indirect-branch-targets") return eExtraIndirectBranchTargets;
  return None;
}


//static_assert((rsp_text_size / instr_size) * instr_size == rsp_text_size, "RSP microcode must be a multiple of the instruction size");

int main(int argc, char* argv[]) {
    std::vector<uint32_t> extra_indirect_branch_targets = {};
    size_t rsp_text_offset = 0x0;
    size_t rsp_text_size = 0x0;
    size_t rsp_text_address = 0x0;
    std::string rom_file_path = "";
    std::string output_file_path = "";
    std::string output_function_name = "";
    std::vector<std::string> arguments(argv + 1, argv + argc);
    int c = 0;
    while (0 < arguments.size()) {
      auto value = arguments.back();
      arguments.pop_back();
      if (convert(value) != None) {
        std::cout << "Invalid Argument order!" << std::endl;
        exit(1);
      }
      auto argument = arguments.back();
      arguments.pop_back();
      switch (convert(argument)) {
        case eRSPTextOffset:
          rsp_text_offset = strtoull(value.erase(0, 2).c_str(), NULL, 16);
          break;
        case eRSPTextAddress:
          rsp_text_address = strtoull(value.erase(0, 2).c_str(), NULL, 16);
          break;
        case eRSPTextSize:
          rsp_text_size = strtoull(value.erase(0, 2).c_str(), NULL, 16);
          break;
        case eRomFilePath:
          rom_file_path = value;
          break;
        case eOutputFilePath:
          output_file_path = value;
          break;
        case eOutputFunctionName:
          output_function_name = value;
          break;
        case eExtraIndirectBranchTargets:
          {
            std::string v = value;
            std::string delimiter = ",";
            while (true) {
              auto t = v.find(delimiter);
              if (t == 0) {
                v.erase(t, 1);
                t = v.find(delimiter);
              }
              if (t == -1) {
                uint32_t converted = strtoul(v.erase(0, 2).c_str(), NULL, 16);
                if (converted != 0) {
                  extra_indirect_branch_targets.push_back(converted);
                }
                break;
              }
              std::string ev = v.substr(0, t);
              v = v.substr(t);
              uint32_t converted = strtoul(ev.erase(0, 2).c_str(), NULL, 16);
              if (converted != 0) {
                extra_indirect_branch_targets.push_back(converted);
              }
            }
            break;
          }
        default:
          std::cout << "Invalid Argument: " << argument << std::endl;
      }
    }
    std::vector<uint32_t> instr_words(rsp_text_size/sizeof(uint32_t));
    {
        std::ifstream rom_file{ rom_file_path, std::ios_base::binary };

        if (!rom_file.good()) {
            fmt::print(stderr, "Failed to open rom file\n");
            return EXIT_FAILURE;
        }

        rom_file.seekg(rsp_text_offset);
        rom_file.read(reinterpret_cast<char*>(instr_words.data()), rsp_text_size);
    }

    // Disable appropriate pseudo instructions
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;

    // Decode the instruction words into instructions
    std::vector<rabbitizer::InstructionRsp> instrs{};
    instrs.reserve(instr_words.size());
    uint32_t vram = rsp_text_address & rsp_mem_mask;
    for (uint32_t instr_word : instr_words) {
        const rabbitizer::InstructionRsp& instr = instrs.emplace_back(byteswap(instr_word), vram);
        vram += instr_size;
    }

    // Collect indirect jump targets (return addresses for linked jumps)
    BranchTargets branch_targets = get_branch_targets(instrs);

    // Add any additional indirect branch targets that may not be found directly in the code (e.g. from a jump table)
    for (uint32_t target : extra_indirect_branch_targets) {
        branch_targets.indirect_targets.insert(target);
    }

    // Open output file and write beginning
    std::filesystem::create_directories(std::filesystem::path{ output_file_path }.parent_path());
    std::ofstream output_file(output_file_path);
    fmt::print(output_file,
        "#include \"rsp.h\"\n"
        "#include \"rsp_vu_impl.h\"\n"
        "RspExitReason {}(uint8_t* rdram) {{\n"
        "    uint32_t           r1 = 0,  r2 = 0,  r3 = 0,  r4 = 0,  r5 = 0,  r6 = 0,  r7 = 0;\n"
        "    uint32_t  r8 = 0,  r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;\n"
        "    uint32_t r16 = 0, r17 = 0, r18 = 0, r19 = 0, r20 = 0, r21 = 0, r22 = 0, r23 = 0;\n"
        "    uint32_t r24 = 0, r25 = 0, r26 = 0, r27 = 0, r28 = 0, r29 = 0, r30 = 0, r31 = 0;\n"
        "    uint32_t dma_dmem_address = 0, dma_dram_address = 0, jump_target = 0;\n"
        "    RSP rsp{{}};\n"
        "    r1 = 0xFC0;\n", output_function_name);
    // Write each instruction
    for (size_t instr_index = 0; instr_index < instrs.size(); instr_index++) {
        process_instruction(instr_index, instrs, output_file, branch_targets, unsupported_instructions, false, false);
    }

    // Terminate instruction code with a return to indicate that the microcode has run past its end
    fmt::print(output_file, "    return RspExitReason::ImemOverrun;\n");

    // Write the section containing the indirect jump table
    write_indirect_jumps(output_file, branch_targets, output_function_name);

    // End the file
    fmt::print(output_file, "}}\n");
    return 0;
}
