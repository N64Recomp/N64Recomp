#include <vector>
#include <set>
#include <unordered_set>

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

bool process_instruction(const RecompPort::Context& context, const RecompPort::Config& config, const RecompPort::Function& func, const RecompPort::FunctionStats& stats, const std::unordered_set<uint32_t>& skipped_insns, size_t instr_index, const std::vector<rabbitizer::InstructionCpu>& instructions, std::ofstream& output_file, bool indent, bool emit_link_branch, int link_branch_index, size_t reloc_index, bool& needs_link_branch, bool& is_branch_likely, std::span<std::vector<uint32_t>> static_funcs_out) {
    const auto& section = context.sections[func.section_index];
    const auto& instr = instructions[instr_index];
    needs_link_branch = false;
    is_branch_likely = false;

    // Output a comment with the original instruction
    if (instr.isBranch() || instr.getUniqueId() == InstrId::cpu_j) {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0, fmt::format("L_{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else if (instr.getUniqueId() == InstrId::cpu_jal) {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0, fmt::format("0x{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else {
        fmt::print(output_file, "    // {}\n", instr.disassemble(0));
    }

    uint32_t instr_vram = instr.getVram();

    if (skipped_insns.contains(instr_vram)) {
        return true;
    }


    bool at_reloc = false;
    bool reloc_handled = false;
    RecompPort::RelocType reloc_type = RecompPort::RelocType::R_MIPS_NONE;
    uint32_t reloc_section = 0;
    uint32_t reloc_target_section_offset = 0;

    // Check if this instruction has a reloc.
    if (section.relocatable && section.relocs.size() > 0 && section.relocs[reloc_index].address == instr_vram) {
        // Get the reloc data for this instruction
        const auto& reloc = section.relocs[reloc_index];
        reloc_section = reloc.target_section;
        // Some symbols are in a nonexistent section (e.g. absolute symbols), so check that the section is valid before doing anything else.
        // Absolute symbols will never need to be relocated so it's safe to skip this.
        if (reloc_section < context.sections.size()) {
            // Ignore this reloc if it points to a different section.
            // Also check if the reloc points to the bss section since that will also be relocated with the section.
            if (reloc_section == func.section_index || reloc_section == section.bss_section_index) {
                // Record the reloc's data.
                reloc_type = reloc.type;
                reloc_target_section_offset = reloc.target_address - section.ram_addr;
                // Ignore all relocs that aren't HI16 or LO16.
                if (reloc_type == RecompPort::RelocType::R_MIPS_HI16 || reloc_type == RecompPort::RelocType::R_MIPS_LO16) {
                    at_reloc = true;
                }
            }
        }
    }

    auto print_indent = [&]() {
        fmt::print(output_file, "    ");
    };

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

    auto print_branch = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts ...args) {
        fmt::print(output_file, "{{\n    ");
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
        fmt::print(output_file, "        ");
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        if (needs_link_branch) {
            fmt::print(output_file, ";\n        goto after_{}", link_branch_index);
        }
        fmt::print(output_file, ";\n    }}\n");
    };

    auto print_func_call = [&](uint32_t target_func_vram) {
        const auto matching_funcs_find = context.functions_by_vram.find(target_func_vram);
        std::string jal_target_name;
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
        needs_link_branch = true;
        print_unconditional_branch("{}(rdram, ctx)", jal_target_name);
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

    uint16_t imm = instr.Get_immediate();

    std::string unsigned_imm_string;
    std::string signed_imm_string;

    uint32_t func_vram_end = func.vram + func.words.size() * sizeof(func.words[0]);
    
    if (!at_reloc) {
        unsigned_imm_string = fmt::format("{:#X}", imm);
        signed_imm_string = fmt::format("{:#X}", (int16_t)imm);
    } else {
        switch (reloc_type) {
            case RecompPort::RelocType::R_MIPS_HI16:
                unsigned_imm_string = fmt::format("RELOC_HI16({}, {:#X})", (uint32_t)func.section_index, reloc_target_section_offset);
                signed_imm_string = "(int16_t)" + unsigned_imm_string;
                reloc_handled = true;
                break;
            case RecompPort::RelocType::R_MIPS_LO16:
                unsigned_imm_string = fmt::format("RELOC_LO16({}, {:#X})", (uint32_t)func.section_index, reloc_target_section_offset);
                signed_imm_string = "(int16_t)" + unsigned_imm_string;
                reloc_handled = true;
                break;
        }
    }

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
    case InstrId::cpu_lui:
        print_line("{}{} = S32({} << 16)", ctx_gpr_prefix(rt), rt, unsigned_imm_string);
        break;
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
        print_line("{}{} = ADD32({}{}, {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_daddu:
        print_line("{}{} = {}{} + {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_negu: // pseudo instruction for subu x, 0, y
    case InstrId::cpu_sub:
    case InstrId::cpu_subu:
        print_line("{}{} = SUB32({}{}, {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_addi:
    case InstrId::cpu_addiu:
        print_line("{}{} = ADD32({}{}, {})", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
        break;
    case InstrId::cpu_daddi:
    case InstrId::cpu_daddiu:
        print_line("{}{} = {}{} + {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
        break;
    case InstrId::cpu_and:
        print_line("{}{} = {}{} & {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_andi:
        print_line("{}{} = {}{} & {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
        break;
    case InstrId::cpu_or:
        print_line("{}{} = {}{} | {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_ori:
        print_line("{}{} = {}{} | {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
        break;
    case InstrId::cpu_nor:
        print_line("{}{} = ~({}{} | {}{})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_xor:
        print_line("{}{} = {}{} ^ {}{}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_xori:
        print_line("{}{} = {}{} ^ {}", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, unsigned_imm_string);
        break;
    case InstrId::cpu_sll:
        print_line("{}{} = S32({}{}) << {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsll:
        print_line("{}{} = {}{} << {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsll32:
        print_line("{}{} = ((gpr)({}{})) << ({} + 32)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_sllv:
        print_line("{}{} = S32({}{}) << ({}{} & 31)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_dsllv:
        print_line("{}{} = {}{} << ({}{} & 63)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_sra:
        print_line("{}{} = S32({}{}) >> {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsra:
        print_line("{}{} = SIGNED({}{}) >> {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsra32:
        print_line("{}{} = SIGNED({}{}) >> ({} + 32)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_srav:
        print_line("{}{} = S32({}{}) >> ({}{} & 31)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_dsrav:
        print_line("{}{} = SIGNED({}{}) >> ({}{} & 63)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_srl:
        print_line("{}{} = S32(U32({}{}) >> {})", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsrl:
        print_line("{}{} = {}{} >> {}", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_dsrl32:
        print_line("{}{} = ((gpr)({}{})) >> ({} + 32)", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, sa);
        break;
    case InstrId::cpu_srlv:
        print_line("{}{} = S32(U32({}{}) >> ({}{} & 31))", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_dsrlv:
        print_line("{}{} = {}{} >> ({}{} & 63))", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_slt:
        print_line("{}{} = SIGNED({}{}) < SIGNED({}{}) ? 1 : 0", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_slti:
        print_line("{}{} = SIGNED({}{}) < {} ? 1 : 0", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
        break;
    case InstrId::cpu_sltu:
        print_line("{}{} = {}{} < {}{} ? 1 : 0", ctx_gpr_prefix(rd), rd, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_sltiu:
        print_line("{}{} = {}{} < {} ? 1 : 0", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, signed_imm_string);
        break;
    case InstrId::cpu_mult:
        print_line("result = S64({}{}) * S64({}{}); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_multu:
        print_line("result = U64({}{}) * U64({}{}); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_div:
        // Cast to 64-bits before division to prevent artihmetic exception for s32(0x80000000) / -1
        print_line("lo = S32(S64(S32({}{})) / S64(S32({}{}))); hi = S32(S64(S32({}{})) % S64(S32({}{})))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_divu:
        print_line("lo = S32(U32({}{}) / U32({}{})); hi = S32(U32({}{}) % U32({}{}))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_mflo:
        print_line("{}{} = lo", ctx_gpr_prefix(rd), rd);
        break;
    case InstrId::cpu_mfhi:
        print_line("{}{} = hi", ctx_gpr_prefix(rd), rd);
        break;
    // Loads
    case InstrId::cpu_ld:
        print_line("{}{} = LD({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lw:
        print_line("{}{} = MEM_W({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lh:
        print_line("{}{} = MEM_H({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lb:
        print_line("{}{} = MEM_B({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lhu:
        print_line("{}{} = MEM_HU({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lbu:
        print_line("{}{} = MEM_BU({}, {}{})", ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
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
    case InstrId::cpu_lwl:
        print_line("{}{} = do_lwl(rdram, {}{}, {}, {}{})", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
    case InstrId::cpu_lwr:
        print_line("{}{} = do_lwr(rdram, {}{}, {}, {}{})", ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rt), rt, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
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
                print_line("void pause_self(uint8_t *rdram); pause_self(rdram)");
            }
            // Check if the branch is within this function
            else if (branch_target >= func.vram && branch_target < func_vram_end) {
                print_unconditional_branch("goto L_{:08X}", branch_target);
            }
            // Otherwise, check if it's a tail call
            else if (instr_vram == func_vram_end - 2 * sizeof(func.words[0])) {
                fmt::print("Tail call in {}\n", func.name);
                print_func_call(branch_target);
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
    case InstrId::cpu_bnel:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bne:
        print_indent();
        print_branch_condition("if ({}{} != {}{})", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_beql:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_beq:
        print_indent();
        print_branch_condition("if ({}{} == {}{})", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_bgezl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bgez:
        print_indent();
        print_branch_condition("if (SIGNED({}{}) >= 0)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_bgtzl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bgtz:
        print_indent();
        print_branch_condition("if (SIGNED({}{}) > 0)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_blezl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_blez:
        print_indent();
        print_branch_condition("if (SIGNED({}{}) <= 0)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_bltzl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bltz:
        print_indent();
        print_branch_condition("if (SIGNED({}{}) < 0)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_break:
        print_line("do_break({})", instr_vram);
        break;

    // Cop1 loads/stores
    case InstrId::cpu_mtc1:
        if ((fs & 1) == 0) {
            // even fpr
            print_line("ctx->f{}.u32l = {}{}", fs, ctx_gpr_prefix(rt), rt);
        }
        else {
            // odd fpr
            print_line("ctx->f_odd[({} - 1) * 2] = {}{}", fs, ctx_gpr_prefix(rt), rt);
        }
        break;
    case InstrId::cpu_mfc1:
        if ((fs & 1) == 0) {
            // even fpr
            print_line("{}{} = (int32_t)ctx->f{}.u32l", ctx_gpr_prefix(rt), rt, fs);
        } else {
            // odd fpr
            print_line("{}{} = (int32_t)ctx->f_odd[({} - 1) * 2]", ctx_gpr_prefix(rt), rt, fs);
        }
        break;
    //case InstrId::cpu_dmfc1:
    //    if ((fs & 1) == 0) {
    //        // even fpr
    //        print_line("{}{} = ctx->f{}.u64", ctx_gpr_prefix(rt), rt, fs);
    //    } else {
    //        fmt::print(stderr, "Invalid operand for dmfc1: f{}\n", fs);
    //        return false;
    //    }
    //    break;
    case InstrId::cpu_lwc1:
        if ((ft & 1) == 0) {
            // even fpr
            print_line("ctx->f{}.u32l = MEM_W({}, {}{})", ft, signed_imm_string, ctx_gpr_prefix(base), base);
        } else {
            // odd fpr
            print_line("ctx->f_odd[({} - 1) * 2] = MEM_W({}, {}{})", ft, signed_imm_string, ctx_gpr_prefix(base), base);
        }
        break;
    case InstrId::cpu_ldc1:
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("ctx->f{}.u64 = LD({}, {}{})", ft, signed_imm_string, ctx_gpr_prefix(base), base);
        break;
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
    case InstrId::cpu_c_lt_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl < ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_olt_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        //print_line("*(volatile int*)0 = 0;");
        print_line("c1cs = ctx->f{}.fl < ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_lt_d:
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
        //print_line("*(volatile int*)0 = 0;");
        print_line("c1cs = ctx->f{}.fl <= ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_le_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d <= ctx->f{}.d", fs, ft);
        break;
    case InstrId::cpu_c_eq_s:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.fl == ctx->f{}.fl", fs, ft);
        break;
    case InstrId::cpu_c_eq_d:
        print_line("CHECK_FR(ctx, {})", fs);
        print_line("CHECK_FR(ctx, {})", ft);
        print_line("c1cs = ctx->f{}.d == ctx->f{}.d", fs, ft);
        break;
    
    // Cop1 branches
    case InstrId::cpu_bc1tl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bc1t:
        print_indent();
        print_branch_condition("if (c1cs)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
        break;
    case InstrId::cpu_bc1fl:
        is_branch_likely = true;
        [[fallthrough]];
    case InstrId::cpu_bc1f:
        print_indent();
        print_branch_condition("if (!c1cs)", ctx_gpr_prefix(rs), rs);
        print_branch("goto L_{:08X}", (uint32_t)instr.getBranchVramGeneric());
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
        fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
        return false;
    }

    // TODO is this used?
    if (emit_link_branch) {
        fmt::print(output_file, "    after_{}:\n", link_branch_index);
    }

    return true;
}

bool compare_files(const std::filesystem::path& file1_path, const std::filesystem::path& file2_path) {
    static std::vector<char> file1_buf(65536);
    static std::vector<char> file2_buf(65536);

    std::ifstream file1(file1_path, std::ifstream::ate | std::ifstream::binary); //open file at the end
    std::ifstream file2(file2_path, std::ifstream::ate | std::ifstream::binary); //open file at the end
    const std::ifstream::pos_type fileSize = file1.tellg();

    file1.rdbuf()->pubsetbuf(file1_buf.data(), file1_buf.size());
    file2.rdbuf()->pubsetbuf(file2_buf.data(), file2_buf.size());

    if (fileSize != file2.tellg()) {
        return false; //different file size
    }

    file1.seekg(0); //rewind
    file2.seekg(0); //rewind

    std::istreambuf_iterator<char> begin1(file1);
    std::istreambuf_iterator<char> begin2(file2);

    return std::equal(begin1, std::istreambuf_iterator<char>(), begin2); //Second argument is end-of-range iterator
}

bool RecompPort::recompile_function(const RecompPort::Context& context, const RecompPort::Config& config, const RecompPort::Function& func, const std::filesystem::path& output_path, std::span<std::vector<uint32_t>> static_funcs_out) {
    //fmt::print("Recompiling {}\n", func.name);
    std::vector<rabbitizer::InstructionCpu> instructions;

    // Open the output file and write the file header
    std::filesystem::path temp_path = output_path;
    temp_path.replace_extension(".tmp");
    std::ofstream output_file{ temp_path };
    if (!output_file.good()) {
        fmt::print(stderr, "Failed to open file for writing: {}\n", temp_path.string() );
        return false;
    }

    fmt::print(output_file,
        "#include \"recomp.h\"\n"
        "#include \"disable_warnings.h\"\n"
        "\n"
        "void {}(uint8_t* rdram, recomp_context* ctx) {{\n"
        // these variables shouldn't need to be preserved across function boundaries, so make them local for more efficient output
        "    uint64_t hi = 0, lo = 0, result = 0;\n"
        "    unsigned int rounding_mode = DEFAULT_ROUNDING_MODE;\n"
        "    int c1cs = 0; \n", // cop1 conditional signal
        func.name);

    // Skip analysis and recompilation of this function is stubbed.
    if (!func.stubbed) {
        // Use a set to sort and deduplicate labels
        std::set<uint32_t> branch_labels;
        instructions.reserve(func.words.size());

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
        bool needs_reloc = section.relocatable && section.relocs.size() > 0;
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

            // If this is a relocatable section, advance the reloc index until we reach the last one or until we get to/pass the current instruction
            if (needs_reloc) {
                while (reloc_index < (section.relocs.size() - 1) && section.relocs[reloc_index].address < vram) {
                    reloc_index++;
                }
            }


            if (section.name == ".anseq") {
                std::this_thread::yield();
            }

            // Process the current instruction and check for errors
            if (process_instruction(context, config, func, stats, skipped_insns, instr_index, instructions, output_file, false, needs_link_branch, num_link_branches, reloc_index, needs_link_branch, is_branch_likely, static_funcs_out) == false) {
                fmt::print(stderr, "Error in recompilation, clearing {}\n", output_path.string());
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

    output_file.close();

    // If a file of the target name exists and it's identical to the output file, delete the output file.
    // This prevents updating the existing file so that it doesn't need to be rebuilt.
    if (std::filesystem::exists(output_path) && compare_files(output_path, temp_path)) {
        std::filesystem::remove(temp_path);
    }
    // Otherwise, rename the new file to the target path.
    else {
        std::filesystem::rename(temp_path, output_path);
    }

    return true;
}
