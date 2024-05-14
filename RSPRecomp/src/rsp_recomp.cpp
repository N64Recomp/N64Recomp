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
#include "toml.hpp"

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
        "    printf(\"Unhandled jump target 0x%04X in microcode {}, coming from [%s:%d]\\n\", jump_target, debug_file, debug_line);\n"
        "    printf(\"Register dump: r0  = %08X r1  = %08X r2  = %08X r3  = %08X r4  = %08X r5  = %08X r6  = %08X r7  = %08X\\n\"\n"
        "           \"               r8  = %08X r9  = %08X r10 = %08X r11 = %08X r12 = %08X r13 = %08X r14 = %08X r15 = %08X\\n\"\n"
        "           \"               r16 = %08X r17 = %08X r18 = %08X r19 = %08X r20 = %08X r21 = %08X r22 = %08X r23 = %08X\\n\"\n"
        "           \"               r24 = %08X r25 = %08X r26 = %08X r27 = %08X r28 = %08X r29 = %08X r30 = %08X r31 = %08X\\n\",\n"
        "           0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16,\n"
        "           r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r29, r30, r31);\n"
        "    return RspExitReason::UnhandledJumpTarget;\n", output_function_name);
}

// TODO de-hardcode these
// OoT njpgdspMain
//constexpr size_t rsp_text_offset = 0xB8BAD0;
//constexpr size_t rsp_text_size = 0xAF0;
//constexpr size_t rsp_text_address = 0x04001080;
//std::string rom_file_path = "../test/oot_mq_debug.z64";
//std::string output_file_path = "../test/rsp/njpgdspMain.cpp";
//std::string output_function_name = "njpgdspMain";
//const std::vector<uint32_t> extra_indirect_branch_targets{};
//const std::unordered_set<uint32_t> unsupported_instructions{};

// OoT aspMain
//constexpr size_t rsp_text_offset = 0xB89260;
//constexpr size_t rsp_text_size = 0xFB0;
//constexpr size_t rsp_text_address = 0x04001000;
//std::string rom_file_path = "../test/oot_mq_debug.z64";
//std::string output_file_path = "../test/rsp/aspMain.cpp";
//std::string output_function_name = "aspMain";
//const std::vector<uint32_t> extra_indirect_branch_targets{ 0x1F68, 0x1230, 0x114C, 0x1F18, 0x1E2C, 0x14F4, 0x1E9C, 0x1CB0, 0x117C, 0x17CC, 0x11E8, 0x1AA4, 0x1B34, 0x1190, 0x1C5C, 0x1220, 0x1784, 0x1830, 0x1A20, 0x1884, 0x1A84, 0x1A94, 0x1A48, 0x1BA0 };
//const std::unordered_set<uint32_t> unsupported_instructions{};

// MM's njpgdspMain is identical to OoT's

//// MM aspMain
//constexpr size_t rsp_text_offset = 0xC40FF0;
//constexpr size_t rsp_text_size = 0x1000;
//constexpr size_t rsp_text_address = 0x04001000;
//std::string rom_file_path = "../../MMRecomp/mm.us.rev1.z64"; // uncompressed rom!
//std::string output_file_path = "../../MMRecomp/rsp/aspMain.cpp";
//std::string output_function_name = "aspMain";
//const std::vector<uint32_t> extra_indirect_branch_targets{ 0x1F80, 0x1250, 0x1154, 0x1094, 0x1E0C, 0x1514, 0x1E7C, 0x1C90, 0x1180, 0x1808, 0x11E8, 0x1ADC, 0x1B6C, 0x1194, 0x1EF8, 0x1240, 0x17C0, 0x186C, 0x1A58, 0x18BC, 0x1ABC, 0x1ACC, 0x1A80, 0x1BD4 };
//const std::unordered_set<uint32_t> unsupported_instructions{};

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

struct RSPRecompilerConfig {
    size_t text_offset;
    size_t text_size;
    size_t text_address;
    std::filesystem::path rom_file_path;
    std::filesystem::path output_file_path;
    std::string output_function_name;
    std::vector<uint32_t> extra_indirect_branch_targets;
    std::unordered_set<uint32_t> unsupported_instructions;
};

std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
	if (!child.empty()) {
		return parent / child;
	}
	return child;
}

template <typename T>
std::vector<T> toml_to_vec(const toml::value& branch_targets_data) {
	std::vector<T> ret;

	if (branch_targets_data.type() != toml::value_t::array) {
		return ret;
	}

	// Get the funcs array as an array type.
	const std::vector<toml::value>& branch_targets_array = branch_targets_data.as_array();

	// Reserve room for all the funcs in the map.
	ret.reserve(branch_targets_array.size());
	for (const toml::value& cur_target_val : branch_targets_array) {
		ret.push_back(cur_target_val.as_integer());
	}

	return ret;
}

bool read_config(const std::filesystem::path& config_path, RSPRecompilerConfig& out) {
    RSPRecompilerConfig ret{};

	try {
		const toml::value config_data = toml::parse(config_path);
		std::filesystem::path basedir = std::filesystem::path{ config_path }.parent_path();

		ret.text_offset           = toml::find<uint32_t>(config_data, "text_offset");
		ret.text_size             = toml::find<uint32_t>(config_data, "text_size");
		ret.text_address          = toml::find<uint32_t>(config_data, "text_address");

		ret.rom_file_path         = concat_if_not_empty(basedir, toml::find<std::string>(config_data, "rom_file_path"));
		ret.output_file_path      = concat_if_not_empty(basedir, toml::find<std::string>(config_data, "output_file_path"));
		ret.output_function_name  = toml::find<std::string>(config_data, "output_function_name");

		// Extra indirect branch targets (optional)
		const toml::value& branch_targets_data = toml::find_or<toml::value>(config_data, "extra_indirect_branch_targets", toml::value{});
		if (branch_targets_data.type() != toml::value_t::empty) {
			ret.extra_indirect_branch_targets = toml_to_vec<uint32_t>(branch_targets_data);
		}

		// Unsupported_instructions (optional)
		const toml::value& unsupported_instructions_data = toml::find_or<toml::value>(config_data, "unsupported_instructions_data", toml::value{});
		if (unsupported_instructions_data.type() != toml::value_t::empty) {
			ret.extra_indirect_branch_targets = toml_to_vec<uint32_t>(unsupported_instructions_data);
		}
	}
	catch (const toml::syntax_error& err) {
		fmt::print(stderr, "Syntax error in config file on line {}, full error:\n{}\n", err.location().line(), err.what());
		return false;
	}
	catch (const toml::type_error& err) {
		fmt::print(stderr, "Incorrect type in config file on line {}, full error:\n{}\n", err.location().line(), err.what());
		return false;
	}
	catch (const std::out_of_range& err) {
		fmt::print(stderr, "Missing value in config file, full error:\n{}\n", err.what());
		return false;
	}

    out = ret;
    return true;
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
    instr_words.resize(config.text_size / sizeof(uint32_t));
    {
        std::ifstream rom_file{ config.rom_file_path, std::ios_base::binary };

        if (!rom_file.good()) {
            fmt::print(stderr, "Failed to open rom file\n");
            return EXIT_FAILURE;
        }

        rom_file.seekg(config.text_offset);
        rom_file.read(reinterpret_cast<char*>(instr_words.data()), config.text_size);
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

    // Collect indirect jump targets (return addresses for linked jumps)
    BranchTargets branch_targets = get_branch_targets(instrs);

    // Add any additional indirect branch targets that may not be found directly in the code (e.g. from a jump table)
    for (uint32_t target : config.extra_indirect_branch_targets) {
        branch_targets.indirect_targets.insert(target);
    }

    // Open output file and write beginning
    std::filesystem::create_directories(std::filesystem::path{ config.output_file_path }.parent_path());
    std::ofstream output_file(config.output_file_path);
    fmt::print(output_file,
        "#include \"rsp.h\"\n"
        "#include \"rsp_vu_impl.h\"\n"
        "RspExitReason {}(uint8_t* rdram) {{\n"
        "    uint32_t           r1 = 0,  r2 = 0,  r3 = 0,  r4 = 0,  r5 = 0,  r6 = 0,  r7 = 0;\n"
        "    uint32_t  r8 = 0,  r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;\n"
        "    uint32_t r16 = 0, r17 = 0, r18 = 0, r19 = 0, r20 = 0, r21 = 0, r22 = 0, r23 = 0;\n"
        "    uint32_t r24 = 0, r25 = 0, r26 = 0, r27 = 0, r28 = 0, r29 = 0, r30 = 0, r31 = 0;\n"
        "    uint32_t dma_dmem_address = 0, dma_dram_address = 0, jump_target = 0;\n"
        "    const char * debug_file = NULL; int debug_line = 0;\n"
        "    RSP rsp{{}};\n"
        "    r1 = 0xFC0;\n", config.output_function_name);
    // Write each instruction
    for (size_t instr_index = 0; instr_index < instrs.size(); instr_index++) {
        process_instruction(instr_index, instrs, output_file, branch_targets, config.unsupported_instructions, false, false);
    }

    // Terminate instruction code with a return to indicate that the microcode has run past its end
    fmt::print(output_file, "    return RspExitReason::ImemOverrun;\n");

    // Write the section containing the indirect jump table
    write_indirect_jumps(output_file, branch_targets, config.output_function_name);

    // End the file
    fmt::print(output_file, "}}\n");
    return 0;
}
