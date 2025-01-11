#include <optional>
#include <fstream>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <filesystem>
#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"
#include <toml++/toml.hpp>

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
        return "SET_DMA_MEM";
    case Cop0Reg::RSP_COP0_SP_RD_LEN:
        return "DO_DMA_READ";
    case Cop0Reg::RSP_COP0_SP_WR_LEN:
        return "DO_DMA_WRITE";
    default:
        fmt::print(stderr, "Unhandled mtc0: {}\n", cop0_reg);
        throw std::runtime_error("Unhandled mtc0");
    }

}

bool is_c0_reg_write_dma_read(int cop0_reg) {
    return static_cast<Cop0Reg>(cop0_reg) == Cop0Reg::RSP_COP0_SP_RD_LEN;
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

struct ResumeTargets {
    std::unordered_set<uint32_t> non_delay_targets;
    std::unordered_set<uint32_t> delay_targets;
};

void get_overlay_swap_resume_targets(const std::vector<rabbitizer::InstructionRsp>& instrs, ResumeTargets& targets) {
    bool is_delay_slot = false;
    for (const auto& instr : instrs) {
        InstrId instr_id = instr.getUniqueId();
        int rd = (int)instr.GetO32_rd();

        if (instr_id == InstrId::rsp_mtc0 && is_c0_reg_write_dma_read(rd)) {
            uint32_t vram = instr.getVram();

            targets.non_delay_targets.insert(vram);

            if (is_delay_slot) {
                targets.delay_targets.insert(vram);
            }
        }

        is_delay_slot = instr.hasDelaySlot();
    }
}

bool process_instruction(size_t instr_index, const std::vector<rabbitizer::InstructionRsp>& instructions, std::ofstream& output_file, const BranchTargets& branch_targets, const std::unordered_set<uint32_t>& unsupported_instructions, const ResumeTargets& resume_targets, bool has_overlays, bool indent, bool in_delay_slot) {
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
            process_instruction(instr_index + 1, instructions, output_file, branch_targets, unsupported_instructions, resume_targets, has_overlays, false, true);
        }
        print_indent();
        fmt::print(output_file, fmt_str, std::forward<Ts>(args)...);
        fmt::print(output_file, ";\n");
    };

    auto print_branch = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts&& ...args) {
        fmt::print(output_file, "{{\n    ");
        if (instr_index < instructions.size() - 1) {
            uint32_t next_vram = instr_vram + 4;
            process_instruction(instr_index + 1, instructions, output_file, branch_targets, unsupported_instructions, resume_targets, has_overlays, true, true);
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
                    operand_string += fmt::format("{}, ", instr.GetRsp_de() & 7);
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
            print_line("debug_file = __FILE__; debug_line = __LINE__");
            print_unconditional_branch("goto do_indirect_jump");
            break;
        case InstrId::rsp_jalr:
            print_line("jump_target = {}{}; {}{} = 0x{:8X}", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rd), rd, instr_vram + 2 * instr_size);
            print_line("debug_file = __FILE__; debug_line = __LINE__");
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
                if (has_overlays && is_c0_reg_write_dma_read(rd)) {
                    // DMA read, do overlay swap if reading into IMEM
                    fmt::print(output_file, 
                        "    if (dma_mem_address & 0x1000) {{\n"
                        "        ctx->resume_address = 0x{:04X};\n"
                        "        ctx->resume_delay = {};\n"
                        "        goto do_overlay_swap;\n"
                        "    }}\n",
                        instr_vram, in_delay_slot ? "true" : "false");
                }
                if (!write_action.empty()) {
                    print_line("{}({}{})", write_action, ctx_gpr_prefix(rt), rt);
                }
                break;
            }
        default:
            fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
            assert(false);
            return false;
        }
    }

    // Write overlay swap resume labels
    if (in_delay_slot) {
        if (resume_targets.delay_targets.contains(instr_vram)) {
            fmt::print(output_file, "R_{:04X}_delay:\n", instr_vram);
        }
    } else {
        if (resume_targets.non_delay_targets.contains(instr_vram)) {
            fmt::print(output_file, "R_{:04X}:\n", instr_vram);
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
        "    printf(\"Unhandled jump target 0x%04X in microcode {}, coming from [%s:%d]\\n\", jump_target, debug_file, debug_line);\n"
        "    printf(\"Register dump: r0  = %08X r1  = %08X r2  = %08X r3  = %08X r4  = %08X r5  = %08X r6  = %08X r7  = %08X\\n\"\n"
        "           \"               r8  = %08X r9  = %08X r10 = %08X r11 = %08X r12 = %08X r13 = %08X r14 = %08X r15 = %08X\\n\"\n"
        "           \"               r16 = %08X r17 = %08X r18 = %08X r19 = %08X r20 = %08X r21 = %08X r22 = %08X r23 = %08X\\n\"\n"
        "           \"               r24 = %08X r25 = %08X r26 = %08X r27 = %08X r28 = %08X r29 = %08X r30 = %08X r31 = %08X\\n\",\n"
        "           0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16,\n"
        "           r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31);\n"
        "    return RspExitReason::UnhandledJumpTarget;\n", output_function_name);
}

void write_overlay_swap_return(std::ofstream& output_file) {
    fmt::print(output_file,
        "do_overlay_swap:\n"
        "                    ctx->r1 = r1;   ctx->r2 = r2;   ctx->r3 = r3;   ctx->r4 = r4;   ctx->r5 = r5;   ctx->r6 = r6;   ctx->r7 = r7;\n"
        "    ctx->r8 = r8;   ctx->r9 = r9;   ctx->r10 = r10; ctx->r11 = r11; ctx->r12 = r12; ctx->r13 = r13; ctx->r14 = r14; ctx->r15 = r15;\n"
        "    ctx->r16 = r16; ctx->r17 = r17; ctx->r18 = r18; ctx->r19 = r19; ctx->r20 = r20; ctx->r21 = r21; ctx->r22 = r22; ctx->r23 = r23;\n"
        "    ctx->r24 = r24; ctx->r25 = r25; ctx->r26 = r26; ctx->r27 = r27; ctx->r28 = r28; ctx->r29 = r29; ctx->r30 = r30; ctx->r31 = r31;\n"
        "    ctx->dma_mem_address = dma_mem_address;\n"
        "    ctx->dma_dram_address = dma_dram_address;\n"
        "    ctx->jump_target = jump_target;\n"
        "    ctx->rsp = rsp;\n"
        "    return RspExitReason::SwapOverlay;\n");
}

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

struct RSPRecompilerOverlayConfig {
    size_t offset;
    size_t size;
};

struct RSPRecompilerOverlaySlotConfig {
    size_t offset;
    std::vector<RSPRecompilerOverlayConfig> overlays;
};

struct RSPRecompilerConfig {
    size_t text_offset;
    size_t text_size;
    size_t text_address;
    std::filesystem::path rom_file_path;
    std::filesystem::path output_file_path;
    std::string output_function_name;
    std::vector<uint32_t> extra_indirect_branch_targets;
    std::unordered_set<uint32_t> unsupported_instructions;
    std::vector<RSPRecompilerOverlaySlotConfig> overlay_slots;
};

std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
    if (!child.empty()) {
        return parent / child;
    }
    return child;
}

template <typename T>
std::vector<T> toml_to_vec(const toml::array* array) {
    std::vector<T> ret;

    // Reserve room for all the funcs in the map.
    ret.reserve(array->size());
    array->for_each([&ret](auto&& el) {
        if constexpr (toml::is_integer<decltype(el)>) {
            ret.push_back(*el);
        }
    });

    return ret;
}

template <typename T>
std::unordered_set<T> toml_to_set(const toml::array* array) {
    std::unordered_set<T> ret;

    array->for_each([&ret](auto&& el) {
        if constexpr (toml::is_integer<decltype(el)>) {
            ret.insert(*el);
        }
    });

    return ret;
}

bool read_config(const std::filesystem::path& config_path, RSPRecompilerConfig& out) {
    RSPRecompilerConfig ret{};

    try {
        const toml::table config_data = toml::parse_file(config_path.u8string());
        std::filesystem::path basedir = std::filesystem::path{ config_path }.parent_path();

        std::optional<uint32_t> text_offset = config_data["text_offset"].value<uint32_t>();
        if (text_offset.has_value()) {
            ret.text_offset = text_offset.value();
        }
        else {
            throw toml::parse_error("Missing text_offset in config file", config_data.source());
        }

        std::optional<uint32_t> text_size = config_data["text_size"].value<uint32_t>();
        if (text_size.has_value()) {
            ret.text_size = text_size.value();
        }
        else {
            throw toml::parse_error("Missing text_size in config file", config_data.source());
        }

        std::optional<uint32_t> text_address = config_data["text_address"].value<uint32_t>();
        if (text_address.has_value()) {
            ret.text_address = text_address.value();
        }
        else {
            throw toml::parse_error("Missing text_address in config file", config_data.source());
        }

        std::optional<std::string> rom_file_path = config_data["rom_file_path"].value<std::string>();
        if (rom_file_path.has_value()) {
            ret.rom_file_path = concat_if_not_empty(basedir, rom_file_path.value());
        }
        else {
            throw toml::parse_error("Missing rom_file_path in config file", config_data.source());
        }

        std::optional<std::string> output_file_path = config_data["output_file_path"].value<std::string>();
        if (output_file_path.has_value()) {
            ret.output_file_path = concat_if_not_empty(basedir, output_file_path.value());
        }
        else {
            throw toml::parse_error("Missing output_file_path in config file", config_data.source());
        }

        std::optional<std::string> output_function_name = config_data["output_function_name"].value<std::string>();
        if (output_function_name.has_value()) {
            ret.output_function_name = output_function_name.value();
        }
        else {
            throw toml::parse_error("Missing output_function_name in config file", config_data.source());
        }

        // Extra indirect branch targets (optional)
        const toml::node_view branch_targets_data = config_data["extra_indirect_branch_targets"];
        if (branch_targets_data.is_array()) {
            const toml::array* branch_targets_array = branch_targets_data.as_array();
            ret.extra_indirect_branch_targets = toml_to_vec<uint32_t>(branch_targets_array);
        }

        // Unsupported_instructions (optional)
        const toml::node_view unsupported_instructions_data = config_data["unsupported_instructions"];
        if (unsupported_instructions_data.is_array()) {
            const toml::array* unsupported_instructions_array = unsupported_instructions_data.as_array();
            ret.unsupported_instructions = toml_to_set<uint32_t>(unsupported_instructions_array);
        }

        // Overlay slots (optional)
        const toml::node_view overlay_slots = config_data["overlay_slots"];
        if (overlay_slots.is_array()) {
            const toml::array* overlay_slots_array = overlay_slots.as_array();

            int slot_idx = 0;
            overlay_slots_array->for_each([&](toml::table slot){
                RSPRecompilerOverlaySlotConfig slot_config;

                std::optional<uint32_t> offset = slot["offset"].value<uint32_t>();
                if (offset.has_value()) {
                    slot_config.offset = offset.value();
                }
                else {
                    throw toml::parse_error(
                        fmt::format("Missing offset in config file at overlay slot {}", slot_idx).c_str(), 
                        config_data.source());
                }

                // Overlays per slot
                const toml::node_view overlays = slot["overlays"];
                if (overlays.is_array()) {
                    const toml::array* overlay_array = overlays.as_array();

                    int overlay_idx = 0;
                    overlay_array->for_each([&](toml::table overlay){
                        RSPRecompilerOverlayConfig overlay_config;
                        
                        std::optional<uint32_t> offset = overlay["offset"].value<uint32_t>();
                        if (offset.has_value()) {
                            overlay_config.offset = offset.value();
                        }
                        else {
                            throw toml::parse_error(
                                fmt::format("Missing offset in config file at overlay slot {} overlay {}", slot_idx, overlay_idx).c_str(), 
                                config_data.source());
                        }

                        std::optional<uint32_t> size = overlay["size"].value<uint32_t>();
                        if (size.has_value()) {
                            overlay_config.size = size.value();

                            if ((size.value() % sizeof(uint32_t)) != 0) {
                                throw toml::parse_error(
                                    fmt::format("Overlay size must be a multiple of {} in config file at overlay slot {} overlay {}", sizeof(uint32_t), slot_idx, overlay_idx).c_str(), 
                                    config_data.source());
                            }
                        }
                        else {
                            throw toml::parse_error(
                                fmt::format("Missing size in config file at overlay slot {} overlay {}", slot_idx, overlay_idx).c_str(), 
                                config_data.source());
                        }

                        slot_config.overlays.push_back(overlay_config);
                        overlay_idx++;
                    });
                }
                else {
                    throw toml::parse_error(
                        fmt::format("Missing overlays in config file at overlay slot {}", slot_idx).c_str(), 
                        config_data.source());
                }

                ret.overlay_slots.push_back(slot_config);
                slot_idx++;
            });
        }

    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return false;
    }

    out = ret;
    return true;
}

struct FunctionPermutation {
    std::vector<rabbitizer::InstructionRsp> instrs;
    std::vector<uint32_t> permutation;
};

struct Permutation {
    std::vector<uint32_t> instr_words;
    std::vector<uint32_t> permutation;
};

struct Overlay {
    std::vector<uint32_t> instr_words;
};

struct OverlaySlot {
    uint32_t offset;
    std::vector<Overlay> overlays;
};

bool next_permutation(const std::vector<uint32_t>& option_lengths, std::vector<uint32_t>& current) {
    current[current.size() - 1] += 1;

    size_t i = current.size() - 1;
    while (current[i] == option_lengths[i]) {
        current[i] = 0;
        if (i == 0) {
            return false;
        }

        current[i - 1] += 1;
        i--;
    }

    return true;
}

void permute(const std::vector<uint32_t>& base_words, const std::vector<OverlaySlot>& overlay_slots, std::vector<Permutation>& permutations) {
    auto current = std::vector<uint32_t>(overlay_slots.size(), 0);
    auto slot_options = std::vector<uint32_t>(overlay_slots.size(), 0);

    for (size_t i = 0; i < overlay_slots.size(); i++) {
        slot_options[i] = overlay_slots[i].overlays.size();
    }

    do {
        Permutation permutation = {
            .instr_words = std::vector<uint32_t>(base_words),
            .permutation = std::vector<uint32_t>(current)
        };

        for (size_t i = 0; i < overlay_slots.size(); i++) {
            const OverlaySlot &slot = overlay_slots[i];
            const Overlay &overlay = slot.overlays[current[i]];

            uint32_t word_offset = slot.offset / sizeof(uint32_t);

            size_t size_needed = word_offset + overlay.instr_words.size();
            if (permutation.instr_words.size() < size_needed) {
                permutation.instr_words.reserve(size_needed);
            }

            std::copy(overlay.instr_words.begin(), overlay.instr_words.end(), permutation.instr_words.data() + word_offset);
        }

        permutations.push_back(permutation);
    } while (next_permutation(slot_options, current));
}

std::string make_permutation_string(const std::vector<uint32_t> permutation) {
    std::string str = "";

    for (uint32_t opt : permutation) {
        str += std::to_string(opt);
    }

    return str;
}

void create_overlay_swap_function(const std::string& function_name, std::ofstream& output_file, const std::vector<FunctionPermutation>& permutations, const RSPRecompilerConfig& config) {
    // Includes and permutation protos
    fmt::print(output_file, 
        "#include <map>\n"
        "#include <vector>\n\n"
        "using RspUcodePermutationFunc = RspExitReason(uint8_t* rdram, RspContext* ctx);\n\n"
        "RspExitReason {}(uint8_t* rdram, RspContext* ctx);\n",
        config.output_function_name + "_initial");

    for (const auto& permutation : permutations) {
        fmt::print(output_file, "RspExitReason {}(uint8_t* rdram, RspContext* ctx);\n",
            config.output_function_name + make_permutation_string(permutation.permutation));
    }
    fmt::print(output_file, "\n");

    // IMEM -> slot index mapping
    fmt::print(output_file, 
        "static const std::map<uint32_t, uint32_t> imemToSlot = {{\n");
    for (size_t i = 0; i < config.overlay_slots.size(); i++) {
        const RSPRecompilerOverlaySlotConfig& slot = config.overlay_slots[i];

        uint32_t imemAddress = (config.text_address & rsp_mem_mask) + slot.offset;
        fmt::print(output_file, "    {{ 0x{:04X}, {} }},\n",
            imemAddress, i);
    }
    fmt::print(output_file, "}};\n\n");

    // ucode offset -> overlay index mapping (per slot)
    fmt::print(output_file, 
        "static const std::vector<std::map<uint32_t, uint32_t>> offsetToOverlay = {{\n");
    for (const auto& slot : config.overlay_slots) {
        fmt::print(output_file, "    {{\n");
        for (size_t i = 0; i < slot.overlays.size(); i++) {
            const RSPRecompilerOverlayConfig& overlay = slot.overlays[i];

            fmt::print(output_file, "        {{ 0x{:04X}, {} }},\n",
                overlay.offset, i);
        }
        fmt::print(output_file, "    }},\n");
    }
    fmt::print(output_file, "}};\n\n");

    // Permutation function pointers
    fmt::print(output_file, 
        "static RspUcodePermutationFunc* permutations[] = {{\n");
    for (const auto& permutation : permutations) {
        fmt::print(output_file, "    {},\n",
            config.output_function_name + make_permutation_string(permutation.permutation));
    }
    fmt::print(output_file, "}};\n\n");

    // Main function
    fmt::print(output_file,
        "RspExitReason {}(uint8_t* rdram, uint32_t ucode_addr) {{\n"
        "    RspContext ctx{{}};\n",
        config.output_function_name);
    
    std::string slots_init_str = "";
    for (size_t i = 0; i < config.overlay_slots.size(); i++) {
        if (i > 0) {
            slots_init_str += ", ";
        }

        slots_init_str += "0";
    }

    fmt::print(output_file, "    uint32_t slots[] = {{{}}};\n\n",
        slots_init_str);

    fmt::print(output_file, "    RspExitReason exitReason = {}(rdram, &ctx);\n\n",
        config.output_function_name + "_initial");
    
    fmt::print(output_file, "");

    std::string perm_index_str = "";
    for (size_t i = 0; i < config.overlay_slots.size(); i++) {
        if (i > 0) {
            perm_index_str += " + ";
        }

        uint32_t multiplier = 1;
        for (size_t k = i + 1; k < config.overlay_slots.size(); k++) {
            multiplier *= config.overlay_slots[k].overlays.size();
        }

        perm_index_str += fmt::format("slots[{}] * {}", i, multiplier);
    }
    
    fmt::print(output_file,
        "    while (exitReason == RspExitReason::SwapOverlay) {{\n"
        "        uint32_t slot = imemToSlot.at(ctx.dma_mem_address);\n"
        "        uint32_t overlay = offsetToOverlay.at(slot).at(ctx.dma_dram_address - ucode_addr);\n"
        "        slots[slot] = overlay;\n"
        "\n"
        "        RspUcodePermutationFunc* permutationFunc = permutations[{}];\n"
        "        exitReason = permutationFunc(rdram, &ctx);\n"
        "    }}\n\n"
        "    return exitReason;\n"
        "}}\n\n",
        perm_index_str);
}

void create_function(const std::string& function_name, std::ofstream& output_file, const std::vector<rabbitizer::InstructionRsp>& instrs, const RSPRecompilerConfig& config, const ResumeTargets& resume_targets, bool is_permutation, bool is_initial) {
    // Collect indirect jump targets (return addresses for linked jumps)
    BranchTargets branch_targets = get_branch_targets(instrs);

    // Add any additional indirect branch targets that may not be found directly in the code (e.g. from a jump table)
    for (uint32_t target : config.extra_indirect_branch_targets) {
        branch_targets.indirect_targets.insert(target);
    }
    
    // Write function
    if (is_permutation) {
        fmt::print(output_file,
            "RspExitReason {}(uint8_t* rdram, RspContext* ctx) {{\n"
            "    uint32_t                 r1 = ctx->r1,   r2 = ctx->r2,   r3 = ctx->r3,   r4 = ctx->r4,   r5 = ctx->r5,   r6 = ctx->r6,   r7 = ctx->r7;\n"
            "    uint32_t  r8 = ctx->r8,  r9 = ctx->r9,   r10 = ctx->r10, r11 = ctx->r11, r12 = ctx->r12, r13 = ctx->r13, r14 = ctx->r14, r15 = ctx->r15;\n"
            "    uint32_t r16 = ctx->r16, r17 = ctx->r17, r18 = ctx->r18, r19 = ctx->r19, r20 = ctx->r20, r21 = ctx->r21, r22 = ctx->r22, r23 = ctx->r23;\n"
            "    uint32_t r24 = ctx->r24, r25 = ctx->r25, r26 = ctx->r26, r27 = ctx->r27, r28 = ctx->r28, r29 = ctx->r29, r30 = ctx->r30, r31 = ctx->r31;\n"
            "    uint32_t dma_mem_address = ctx->dma_mem_address, dma_dram_address = ctx->dma_dram_address, jump_target = ctx->jump_target;\n"
            "    const char * debug_file = NULL; int debug_line = 0;\n"
            "    RSP rsp = ctx->rsp;\n", function_name);

        // Write jumps to resume targets
        if (!is_initial) {
            fmt::print(output_file,
                "    if (ctx->resume_delay) {{\n"
                "        switch (ctx->resume_address) {{\n");
            
            for (uint32_t address : resume_targets.delay_targets) {
                fmt::print(output_file, "            case 0x{0:04X}: goto R_{0:04X}_delay;\n", 
                    address);
            }
            
            fmt::print(output_file,
                "        }}\n"
                "    }} else {{\n"
                "        switch (ctx->resume_address) {{\n");
            
            for (uint32_t address : resume_targets.non_delay_targets) {
                fmt::print(output_file, "            case 0x{0:04X}: goto R_{0:04X};\n", 
                    address);
            }

            fmt::print(output_file,
                "        }}\n"
                "    }}\n"
                "    printf(\"Unhandled resume target 0x%04X (delay slot: %d) in microcode {}\\n\", ctx->resume_address, ctx->resume_delay);\n"
                "    return RspExitReason::UnhandledResumeTarget;\n",
                config.output_function_name);
        }

        fmt::print(output_file, "    r1 = 0xFC0;\n");
    } else {
        fmt::print(output_file,
            "RspExitReason {}(uint8_t* rdram) {{\n"
            "    uint32_t           r1 = 0,  r2 = 0,  r3 = 0,  r4 = 0,  r5 = 0,  r6 = 0,  r7 = 0;\n"
            "    uint32_t  r8 = 0,  r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;\n"
            "    uint32_t r16 = 0, r17 = 0, r18 = 0, r19 = 0, r20 = 0, r21 = 0, r22 = 0, r23 = 0;\n"
            "    uint32_t r24 = 0, r25 = 0, r26 = 0, r27 = 0, r28 = 0, r29 = 0, r30 = 0, r31 = 0;\n"
            "    uint32_t dma_mem_address = 0, dma_dram_address = 0, jump_target = 0;\n"
            "    const char * debug_file = NULL; int debug_line = 0;\n"
            "    RSP rsp{{}};\n"
            "    r1 = 0xFC0;\n", function_name);
    }
    // Write each instruction
    for (size_t instr_index = 0; instr_index < instrs.size(); instr_index++) {
        process_instruction(instr_index, instrs, output_file, branch_targets, config.unsupported_instructions, resume_targets, is_permutation, false, false);
    }

    // Terminate instruction code with a return to indicate that the microcode has run past its end
    fmt::print(output_file, "    return RspExitReason::ImemOverrun;\n");

    // Write the section containing the indirect jump table
    write_indirect_jumps(output_file, branch_targets, config.output_function_name);

    // Write routine for returning for an overlay swap
    if (is_permutation) {
        write_overlay_swap_return(output_file);
    }

    // End the file
    fmt::print(output_file, "}}\n");
}

int main(int argc, const char** argv) {
    if (argc != 2) {
        fmt::print("Usage: {} [config file]\n", argv[0]);
        std::exit(EXIT_SUCCESS);
    }

    RSPRecompilerConfig config;
    if (!read_config(std::filesystem::path{argv[1]}, config)) {
        fmt::print("Failed to parse config file {}\n", argv[0]);
        std::exit(EXIT_FAILURE);
    }

    std::vector<uint32_t> instr_words{};
    std::vector<OverlaySlot> overlay_slots{};
    instr_words.resize(config.text_size / sizeof(uint32_t));
    {
        std::ifstream rom_file{ config.rom_file_path, std::ios_base::binary };

        if (!rom_file.good()) {
            fmt::print(stderr, "Failed to open rom file\n");
            return EXIT_FAILURE;
        }

        rom_file.seekg(config.text_offset);
        rom_file.read(reinterpret_cast<char*>(instr_words.data()), config.text_size);

        for (const RSPRecompilerOverlaySlotConfig &slot_config : config.overlay_slots) {
            OverlaySlot slot{};
            slot.offset = slot_config.offset;

            for (const RSPRecompilerOverlayConfig &overlay_config : slot_config.overlays) {
                Overlay overlay{};
                overlay.instr_words.resize(overlay_config.size / sizeof(uint32_t));

                rom_file.seekg(config.text_offset + overlay_config.offset);
                rom_file.read(reinterpret_cast<char*>(overlay.instr_words.data()), overlay_config.size);

                slot.overlays.push_back(overlay);
            }

            overlay_slots.push_back(slot);
        }
    }

    // Create overlay permutations
    std::vector<Permutation> permutations{};
    if (!overlay_slots.empty()) {
        permute(instr_words, overlay_slots, permutations);
    }

    // Disable appropriate pseudo instructions
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;

    // Decode the instruction words into instructions
    std::vector<rabbitizer::InstructionRsp> instrs{};
    instrs.reserve(instr_words.size());
    uint32_t vram = config.text_address & rsp_mem_mask;
    for (uint32_t instr_word : instr_words) {
        const rabbitizer::InstructionRsp& instr = instrs.emplace_back(byteswap(instr_word), vram);
        vram += instr_size;
    }

    std::vector<FunctionPermutation> func_permutations{};
    func_permutations.reserve(permutations.size());
    for (const Permutation& permutation : permutations) {
        FunctionPermutation func = {
            .permutation = std::vector<uint32_t>(permutation.permutation)
        };

        func.instrs.reserve(permutation.instr_words.size());
        uint32_t vram = config.text_address & rsp_mem_mask;
        for (uint32_t instr_word : permutation.instr_words) {
            const rabbitizer::InstructionRsp& instr = func.instrs.emplace_back(byteswap(instr_word), vram);
            vram += instr_size;
        }

        func_permutations.emplace_back(func);
    }

    // Determine all possible overlay swap resume targets
    ResumeTargets resume_targets{};
    for (const FunctionPermutation& permutation : func_permutations) {
        get_overlay_swap_resume_targets(permutation.instrs, resume_targets);
    }

    // Open output file and write beginning
    std::filesystem::create_directories(std::filesystem::path{ config.output_file_path }.parent_path());
    std::ofstream output_file(config.output_file_path);
    fmt::print(output_file,
        "#include \"librecomp/rsp.hpp\"\n"
        "#include \"librecomp/rsp_vu_impl.hpp\"\n");
    
    // Write function(s)
    if (overlay_slots.empty()) {
        create_function(config.output_function_name, output_file, instrs, config, resume_targets, false, false);
    } else {
        create_overlay_swap_function(config.output_function_name, output_file, func_permutations, config);
        create_function(config.output_function_name + "_initial", output_file, instrs, config, ResumeTargets{}, true, true);

        for (const auto& permutation : func_permutations) {
            create_function(config.output_function_name + make_permutation_string(permutation.permutation), 
                output_file, permutation.instrs, config, resume_targets, true, false);
        }
    }

    return 0;
}
