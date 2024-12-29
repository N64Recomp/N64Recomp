#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/context.h"
#include "analysis.h"
#include "recompiler/operations.h"
#include "recompiler/generator.h"

enum class JalResolutionResult {
    NoMatch,
    Match,
    CreateStatic,
    Ambiguous,
    Error
};

JalResolutionResult resolve_jal(const N64Recomp::Context& context, size_t cur_section_index, uint32_t target_func_vram, size_t& matched_function_index) {
    // Look for symbols with the target vram address
    const N64Recomp::Section& cur_section = context.sections[cur_section_index];
    const auto matching_funcs_find = context.functions_by_vram.find(target_func_vram);
    uint32_t section_vram_start = cur_section.ram_addr;
    uint32_t section_vram_end = cur_section.ram_addr + cur_section.size;
    bool in_current_section = target_func_vram >= section_vram_start && target_func_vram < section_vram_end;
    bool exact_match_found = false;

    // Use a thread local to prevent reallocation across runs and to allow multi-threading in the future.
    thread_local std::vector<size_t> matched_funcs{};
    matched_funcs.clear();

    // Evaluate any functions with the target address to see if they're potential candidates for JAL resolution.
    if (matching_funcs_find != context.functions_by_vram.end()) {
        for (size_t target_func_index : matching_funcs_find->second) {
            const auto& target_func = context.functions[target_func_index];

            // Zero-sized symbol handling. unless there's only one matching target.
            if (target_func.words.empty()) {
                // Allow zero-sized symbols between 0x8F000000 and 0x90000000 for use with patches.
                // TODO make this configurable or come up with a more sensible solution for dealing with manual symbols for patches.
                if (target_func.vram < 0x8F000000 || target_func.vram > 0x90000000) {
                    continue;
                }
            }

            // Immediately accept a function in the same section as this one, since it must also be loaded if the current function is.
            if (target_func.section_index == cur_section_index) {
                exact_match_found = true;
                matched_funcs.clear();
                matched_funcs.push_back(target_func_index);
                break;
            }

            // If the function's section isn't relocatable, add the function as a candidate.
            const auto& target_func_section = context.sections[target_func.section_index];
            if (!target_func_section.relocatable) {
                matched_funcs.push_back(target_func_index);
            }
        }
    }

    // If the target vram is in the current section, only allow exact matches.
    if (in_current_section) {
        // If an exact match was found, use it.
        if (exact_match_found) {
            matched_function_index = matched_funcs[0];
            return JalResolutionResult::Match;
        }
        // Otherwise, create a static function at the target address.
        else {
            return JalResolutionResult::CreateStatic;
        }
    }
    // Otherwise, disambiguate based on the matches found.
    else {
        // If there were no matches then JAL resolution has failed.
        // A static can't be created as the target section is unknown.
        if (matched_funcs.size() == 0) {
            return JalResolutionResult::NoMatch;
        }
        // If there was an exact match, use it.
        else if (matched_funcs.size() == 1) {
            matched_function_index = matched_funcs[0];
            return JalResolutionResult::Match;
        }
        // If there's more than one match, use an indirect jump to resolve the function at runtime.
        else {
            return JalResolutionResult::Ambiguous;
        }
    }

    // This should never be hit, so return an error.
    return JalResolutionResult::Error;
}

using InstrId = rabbitizer::InstrId::UniqueId;
using Cop0Reg = rabbitizer::Registers::Cpu::Cop0;

std::string_view ctx_gpr_prefix(int reg) {
    if (reg != 0) {
        return "ctx->r";
    }
    return "";
}

template <typename GeneratorType>
bool process_instruction(GeneratorType& generator, const N64Recomp::Context& context, const N64Recomp::Function& func, const N64Recomp::FunctionStats& stats, const std::unordered_set<uint32_t>& jtbl_lw_instructions, size_t instr_index, const std::vector<rabbitizer::InstructionCpu>& instructions, std::ostream& output_file, bool indent, bool emit_link_branch, int link_branch_index, size_t reloc_index, bool& needs_link_branch, bool& is_branch_likely, bool tag_reference_relocs, std::span<std::vector<uint32_t>> static_funcs_out) {
    using namespace N64Recomp;

    const auto& section = context.sections[func.section_index];
    const auto& instr = instructions[instr_index];
    needs_link_branch = false;
    is_branch_likely = false;
    uint32_t instr_vram = instr.getVram();
    InstrId instr_id = instr.getUniqueId();

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
    print_indent();
    if (instr.isBranch() || instr_id == InstrId::cpu_j) {
        generator.emit_comment(fmt::format("0x{:08X}: {}", instr_vram, instr.disassemble(0, fmt::format("L_{:08X}", (uint32_t)instr.getBranchVramGeneric()))));
    } else if (instr_id == InstrId::cpu_jal) {
        generator.emit_comment(fmt::format("0x{:08X}: {}", instr_vram, instr.disassemble(0, fmt::format("0x{:08X}", (uint32_t)instr.getBranchVramGeneric()))));
    } else {
        generator.emit_comment(fmt::format("0x{:08X}: {}", instr_vram, instr.disassemble(0)));
    }

    // Replace loads for jump table entries into addiu. This leaves the jump table entry's address in the output register
    // instead of the entry's value, which can then be used to determine the offset from the start of the jump table.
    if (jtbl_lw_instructions.contains(instr_vram)) {
        assert(instr_id == InstrId::cpu_lw);
        instr_id = InstrId::cpu_addiu;
    }

    N64Recomp::RelocType reloc_type = N64Recomp::RelocType::R_MIPS_NONE;
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

        // Check if the relocation references a relocatable section.
        bool target_relocatable = false;
        if (!reloc.reference_symbol && reloc_section != N64Recomp::SectionAbsolute) {
            const auto& target_section = context.sections[reloc_section];
            target_relocatable = target_section.relocatable;
        }

        // Only process this relocation if the target section is relocatable or if this relocation targets a reference symbol.
        if (target_relocatable || reloc.reference_symbol) {
            // Record the reloc's data.
            reloc_type = reloc.type;
            reloc_target_section_offset = reloc.target_section_offset;
            // Ignore all relocs that aren't MIPS_HI16, MIPS_LO16 or MIPS_26.
            if (reloc_type == N64Recomp::RelocType::R_MIPS_HI16 || reloc_type == N64Recomp::RelocType::R_MIPS_LO16 || reloc_type == N64Recomp::RelocType::R_MIPS_26) {
                if (reloc.reference_symbol) {
                    reloc_reference_symbol = reloc.symbol_index;
                    // Don't try to relocate special section symbols.
                    if (context.is_regular_reference_section(reloc.target_section) || reloc_section == N64Recomp::SectionAbsolute) {
                        bool ref_section_relocatable = context.is_reference_section_relocatable(reloc.target_section);
                        // Resolve HI16 and LO16 reference symbol relocs to non-relocatable sections by patching the instruction immediate.
                        if (!ref_section_relocatable && (reloc_type == N64Recomp::RelocType::R_MIPS_HI16 || reloc_type == N64Recomp::RelocType::R_MIPS_LO16)) {
                            uint32_t ref_section_vram = context.get_reference_section_vram(reloc.target_section);
                            uint32_t full_immediate = reloc.target_section_offset + ref_section_vram;

                            if (reloc_type == N64Recomp::RelocType::R_MIPS_HI16) {
                                imm = (full_immediate >> 16) + ((full_immediate >> 15) & 1);
                            }
                            else if (reloc_type == N64Recomp::RelocType::R_MIPS_LO16) {
                                imm = full_immediate & 0xFFFF;
                            }

                            // The reloc has been processed, so set it to none to prevent it getting processed a second time during instruction code generation.
                            reloc_type = N64Recomp::RelocType::R_MIPS_NONE;
                            reloc_reference_symbol = (size_t)-1;
                        }
                    }
                }
            }

            // Repoint bss relocations at their non-bss counterpart section.
            auto find_bss_it = context.bss_section_to_section.find(reloc_section);
            if (find_bss_it != context.bss_section_to_section.end()) {
                reloc_section = find_bss_it->second;
            }
        }
    }

    auto process_delay_slot = [&](bool use_indent) {
        if (instr_index < instructions.size() - 1) {
            bool dummy_needs_link_branch;
            bool dummy_is_branch_likely;
            size_t next_reloc_index = reloc_index;
            uint32_t next_vram = instr_vram + 4;
            if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                next_reloc_index++;
            }
            if (!process_instruction(generator, context, func, stats, jtbl_lw_instructions, instr_index + 1, instructions, output_file, use_indent, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, tag_reference_relocs, static_funcs_out)) {
                return false;
            }
        }
        return true;
    };

    auto print_link_branch = [&]() {
        if (needs_link_branch) {
            print_indent();
            generator.emit_goto(fmt::format("after_{}", link_branch_index));
        }
    };

    auto print_return_with_delay_slot = [&]() {
        if (!process_delay_slot(false)) {
            return false;
        }
        print_indent();
        generator.emit_return();
        print_link_branch();
        return true;
    };

    auto print_goto_with_delay_slot = [&](const std::string& target) {
        if (!process_delay_slot(false)) {
            return false;
        }
        print_indent();
        generator.emit_goto(target);
        print_link_branch();
        return true;
    };

    auto print_func_call_by_register = [&](int reg) {
        if (!process_delay_slot(false)) {
            return false;
        }
        print_indent();
        generator.emit_function_call_by_register(reg);
        print_link_branch();
        return true;
    };

    auto print_func_call_by_address = [&generator, reloc_target_section_offset, reloc_section, reloc_reference_symbol, reloc_type, &context, &func, &static_funcs_out, &needs_link_branch, &print_indent, &process_delay_slot, &print_link_branch]
        (uint32_t target_func_vram, bool tail_call = false, bool indent = false)
    {
        bool call_by_lookup = false;
        // Event symbol, emit a call to the runtime to trigger this event.
        if (reloc_section == N64Recomp::SectionEvent) {
            needs_link_branch = !tail_call;
            if (indent) {
                print_indent();
            }
            if (!process_delay_slot(false)) {
                return false;
            }
            print_indent();
            generator.emit_trigger_event((uint32_t)reloc_reference_symbol);
            print_link_branch();
        }
        // Normal symbol or reference symbol, 
        else {
            std::string jal_target_name{};
            size_t matched_func_index = (size_t)-1;
            if (reloc_reference_symbol != (size_t)-1) {
                if (reloc_type != N64Recomp::RelocType::R_MIPS_26) {
                    fmt::print(stderr, "Unsupported reloc type {} on jal instruction in {}\n", (int)reloc_type, func.name);
                    return false;
                }

                if (!context.skip_validating_reference_symbols) {
                    const auto& ref_symbol = context.get_reference_symbol(reloc_section, reloc_reference_symbol);
                    if (ref_symbol.section_offset != reloc_target_section_offset) {
                        fmt::print(stderr, "Function {} uses a MIPS_R_26 addend, which is not supported yet\n", func.name);
                        return false;
                    }
                }
            }
            else {
                JalResolutionResult jal_result = resolve_jal(context, func.section_index, target_func_vram, matched_func_index);

                switch (jal_result) {
                    case JalResolutionResult::NoMatch:
                        fmt::print(stderr, "No function found for jal target: 0x{:08X}\n", target_func_vram);
                        return false;
                    case JalResolutionResult::Match:
                        jal_target_name = context.functions[matched_func_index].name;
                        break;
                    case JalResolutionResult::CreateStatic:
                        // Create a static function add it to the static function list for this section.
                        jal_target_name = fmt::format("static_{}_{:08X}", func.section_index, target_func_vram);
                        static_funcs_out[func.section_index].push_back(target_func_vram);
                        // TODO skip lookup for static functions.
                        call_by_lookup = true;
                        break;
                    case JalResolutionResult::Ambiguous:
                        fmt::print(stderr, "[Info] Ambiguous jal target 0x{:08X} in function {}, falling back to function lookup\n", target_func_vram, func.name);
                        // Relocation isn't necessary for jumps inside a relocatable section, as this code path will never run if the target vram
                        // is in the current function's section (see the branch for `in_current_section` above).
                        // If a game ever needs to jump between multiple relocatable sections, relocation will be necessary here.
                        call_by_lookup = true;
                        break;
                    case JalResolutionResult::Error:
                        fmt::print(stderr, "Internal error when resolving jal to address 0x{:08X} in function {}. Please report this issue.\n", target_func_vram, func.name);
                        return false;
                }
            }
            needs_link_branch = !tail_call;
            if (indent) {
                print_indent();
            }
            if (!process_delay_slot(false)) {
                return false;
            }
            print_indent();
            if (reloc_reference_symbol != (size_t)-1) {
                generator.emit_function_call_reference_symbol(context, reloc_section, reloc_reference_symbol, reloc_target_section_offset);
            }
            else if (call_by_lookup) {
                generator.emit_function_call_lookup(target_func_vram);
            }
            else {
                generator.emit_function_call(context, matched_func_index);
            }
            print_link_branch();
        }
        return true;
    };

    auto print_branch = [&](uint32_t branch_target) {
        // If the branch target is outside the current function, check if it can be treated as a tail call.
        if (branch_target < func.vram || branch_target >= func_vram_end) {
            // If the branch target is the start of some known function, this can be handled as a tail call.
            // FIXME: how to deal with static functions?
            if (context.functions_by_vram.find(branch_target) != context.functions_by_vram.end()) {
                fmt::print("Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                if (!print_func_call_by_address(branch_target, true, true)) {
                    return false;
                }
                print_indent();
                generator.emit_return();
                // TODO check if this branch close should exist.
                // print_indent();
                // generator.emit_branch_close();
                return true;
            }

            fmt::print(stderr, "[Warn] Function {} is branching outside of the function (to 0x{:08X})\n", func.name, branch_target);
        }

        if (!process_delay_slot(true)) {
            return false;
        }

        print_indent();
        print_indent();
        generator.emit_goto(fmt::format("L_{:08X}", branch_target));
        // TODO check if this link branch ever exists.
        if (needs_link_branch) {
            print_indent();
            print_indent();
            generator.emit_goto(fmt::format("after_{}", link_branch_index));
        }
        return true;
    };

    if (indent) {
        print_indent();
    }

    int rd = (int)instr.GetO32_rd();
    int rs = (int)instr.GetO32_rs();
    int rt = (int)instr.GetO32_rt();
    int sa = (int)instr.Get_sa();

    int fd = (int)instr.GetO32_fd();
    int fs = (int)instr.GetO32_fs();
    int ft = (int)instr.GetO32_ft();

    int cop1_cs = (int)instr.Get_cop1cs();

    bool handled = true;

    switch (instr_id) {
    case InstrId::cpu_nop:
        fmt::print(output_file, "\n");
        break;
    // Cop0 (Limited functionality)
    case InstrId::cpu_mfc0:
        {
            Cop0Reg reg = instr.Get_cop0d();
            switch (reg) {
            case Cop0Reg::COP0_Status:
                print_indent();
                generator.emit_cop0_status_read(rt);
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
                print_indent();
                generator.emit_cop0_status_write(rt);
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
                [instr_vram](const N64Recomp::JumpTable& jtbl) {
                return jtbl.addu_vram == instr_vram;
            });
            // If so, create a temp to preserve the addend register's value
            if (find_result != stats.jump_tables.end()) {
                const N64Recomp::JumpTable& cur_jtbl = *find_result;
                print_indent();
                generator.emit_jtbl_addend_declaration(cur_jtbl, cur_jtbl.addend_reg);
            }
        }
        break;
    case InstrId::cpu_mult:
    case InstrId::cpu_dmult:
    case InstrId::cpu_multu:
    case InstrId::cpu_dmultu:
    case InstrId::cpu_div:
    case InstrId::cpu_ddiv:
    case InstrId::cpu_divu:
    case InstrId::cpu_ddivu:
        print_indent();
        generator.emit_muldiv(instr_id, rs, rt);
        break;
    // Branches
    case InstrId::cpu_jal:
        if (!print_func_call_by_address(instr.getBranchVramGeneric())) {
            return false;
        }
        break;
    case InstrId::cpu_jalr:
        // jalr can only be handled with $ra as the return address register
        if (rd != (int)rabbitizer::Registers::Cpu::GprO32::GPR_O32_ra) {
            fmt::print(stderr, "Invalid return address reg for jalr: f{}\n", rd);
            return false;
        }
        needs_link_branch = true;
        print_func_call_by_register(rs);
        break;
    case InstrId::cpu_j:
    case InstrId::cpu_b:
        {
            uint32_t branch_target = instr.getBranchVramGeneric();
            if (branch_target == instr_vram) {
                print_indent();
                generator.emit_pause_self();
            }
            // Check if the branch is within this function
            else if (branch_target >= func.vram && branch_target < func_vram_end) {
                print_goto_with_delay_slot(fmt::format("L_{:08X}", branch_target));
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
                fmt::print("[Info] Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                if (!print_func_call_by_address(branch_target, true)) {
                    return false;
                }
                print_indent();
                generator.emit_return();
            }
            else {
                fmt::print(stderr, "Unhandled branch in {} at 0x{:08X} to 0x{:08X}\n", func.name, instr_vram, branch_target);
                return false;
            }
        }
        break;
    case InstrId::cpu_jr:
        if (rs == (int)rabbitizer::Registers::Cpu::GprO32::GPR_O32_ra) {
            print_return_with_delay_slot();
        } else {
            auto jtbl_find_result = std::find_if(stats.jump_tables.begin(), stats.jump_tables.end(),
                [instr_vram](const N64Recomp::JumpTable& jtbl) {
                    return jtbl.jr_vram == instr_vram;
                });

            if (jtbl_find_result != stats.jump_tables.end()) {
                const N64Recomp::JumpTable& cur_jtbl = *jtbl_find_result;
                if (!process_delay_slot(false)) {
                    return false;
                }
                print_indent();
                generator.emit_switch(cur_jtbl, rs);
                for (size_t entry_index = 0; entry_index < cur_jtbl.entries.size(); entry_index++) {
                    print_indent();
                    print_indent();
                    generator.emit_case(entry_index, fmt::format("L_{:08X}", cur_jtbl.entries[entry_index]));
                }
                print_indent();
                print_indent();
                generator.emit_switch_error(instr_vram, cur_jtbl.vram);
                print_indent();
                generator.emit_switch_close();
                break;
            }

            fmt::print("[Info] Indirect tail call in {}\n", func.name);
            print_func_call_by_register(rs);
            print_indent();
            generator.emit_return();
            break;
        }
        break;
    case InstrId::cpu_syscall:
        print_indent();
        generator.emit_syscall(instr_vram);
        // syscalls don't link, so treat it like a tail call
        print_indent();
        generator.emit_return();
        break;
    case InstrId::cpu_break:
        print_indent();
        generator.emit_do_break(instr_vram);
        break;

    // Cop1 rounding mode
    case InstrId::cpu_ctc1:
        if (cop1_cs != 31) {
            fmt::print(stderr, "Invalid FP control register for ctc1: {}\n", cop1_cs);
            return false;
        }
        print_indent();
        generator.emit_cop1_cs_write(rt);
        break;
    case InstrId::cpu_cfc1:
        if (cop1_cs != 31) {
            fmt::print(stderr, "Invalid FP control register for cfc1: {}\n", cop1_cs);
            return false;
        }
        print_indent();
        generator.emit_cop1_cs_read(rt);
        break;
    default:
        handled = false;
        break;
    }

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
    instruction_context.reloc_tag_as_reference = (reloc_reference_symbol != (size_t)-1) && tag_reference_relocs;
    instruction_context.reloc_type = reloc_type;
    instruction_context.reloc_section_index = reloc_section;
    instruction_context.reloc_target_section_offset = reloc_target_section_offset;
    
    auto do_check_fr = [](const GeneratorType& generator, const InstructionContext& ctx, Operand operand) {
        switch (operand) {
            case Operand::Fd:
            case Operand::FdDouble:
            case Operand::FdU32L:
            case Operand::FdU32H:
            case Operand::FdU64:
                generator.emit_check_fr(ctx.fd);
                break;
            case Operand::Fs:
            case Operand::FsDouble:
            case Operand::FsU32L:
            case Operand::FsU32H:
            case Operand::FsU64:
                generator.emit_check_fr(ctx.fs);
                break;
            case Operand::Ft:
            case Operand::FtDouble:
            case Operand::FtU32L:
            case Operand::FtU32H:
            case Operand::FtU64:
                generator.emit_check_fr(ctx.ft);
                break;
            default:
                // No MIPS3 float check needed for non-float operands.
                break;
        }
    };
    
    auto do_check_nan = [](const GeneratorType& generator, const InstructionContext& ctx, Operand operand) {
        switch (operand) {
            case Operand::Fd:
                generator.emit_check_nan(ctx.fd, false);
                break;
            case Operand::Fs:
                generator.emit_check_nan(ctx.fs, false);
                break;
            case Operand::Ft:
                generator.emit_check_nan(ctx.ft, false);
                break;
            case Operand::FdDouble:
                generator.emit_check_nan(ctx.fd, true);
                break;
            case Operand::FsDouble:
                generator.emit_check_nan(ctx.fs, true);
                break;
            case Operand::FtDouble:
                generator.emit_check_nan(ctx.ft, true);
                break;
            default:
                // No NaN checks needed for non-float operands.
                break;
        }
    };

    auto find_binary_it = binary_ops.find(instr_id);
    if (find_binary_it != binary_ops.end()) {
        print_indent();
        const BinaryOp& op = find_binary_it->second;
        
        if (op.check_fr) {
            do_check_fr(generator, instruction_context, op.output);
            do_check_fr(generator, instruction_context, op.operands.operands[0]);
            do_check_fr(generator, instruction_context, op.operands.operands[1]);
        }

        if (op.check_nan) {
            do_check_nan(generator, instruction_context, op.operands.operands[0]);
            do_check_nan(generator, instruction_context, op.operands.operands[1]);
            fmt::print(output_file, "\n");
            print_indent();
        }

        generator.process_binary_op(op, instruction_context);
        handled = true;
    }

    auto find_unary_it = unary_ops.find(instr_id);
    if (find_unary_it != unary_ops.end()) {
        print_indent();
        const UnaryOp& op = find_unary_it->second;
        
        if (op.check_fr) {
            do_check_fr(generator, instruction_context, op.output);
            do_check_fr(generator, instruction_context, op.input);
        }

        if (op.check_nan) {
            do_check_nan(generator, instruction_context, op.input);
            fmt::print(output_file, "\n");
            print_indent();
        }

        generator.process_unary_op(op, instruction_context);
        handled = true;
    }

    auto find_conditional_branch_it = conditional_branch_ops.find(instr_id);
    if (find_conditional_branch_it != conditional_branch_ops.end()) {
        print_indent();
        // TODO combining the branch condition and branch target into one generator call would allow better optimization in the runtime's JIT generator.
        // This would require splitting into a conditional jump method and conditional function call method.
        generator.emit_branch_condition(find_conditional_branch_it->second, instruction_context);

        print_indent();
        if (find_conditional_branch_it->second.link) {
            if (!print_func_call_by_address(instr.getBranchVramGeneric())) {
                return false;
            }
        }
        else {
            if (!print_branch((uint32_t)instr.getBranchVramGeneric())) {
                return false;
            }
        }

        print_indent();
        generator.emit_branch_close();
        
        is_branch_likely = find_conditional_branch_it->second.likely;
        handled = true;
    }

    auto find_store_it = store_ops.find(instr_id);
    if (find_store_it != store_ops.end()) {
        print_indent();
        const StoreOp& op = find_store_it->second;

        if (op.type == StoreOpType::SDC1) {
            do_check_fr(generator, instruction_context, op.value_input);
        }

        generator.process_store_op(op, instruction_context);
        handled = true;
    }

    if (!handled) {
        fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
        return false;
    }

    // TODO is this used?
    if (emit_link_branch) {
        print_indent();
        generator.emit_label(fmt::format("after_{}", link_branch_index));
    }

    return true;
}

template <typename GeneratorType>
bool recompile_function_impl(GeneratorType& generator, const N64Recomp::Context& context, size_t func_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    const N64Recomp::Function& func = context.functions[func_index];
    //fmt::print("Recompiling {}\n", func.name);
    std::vector<rabbitizer::InstructionCpu> instructions;

    generator.emit_function_start(func.name, func_index);

    if (context.trace_mode) {
        fmt::print(output_file,
            "    TRACE_ENTRY();",
            func.name);
    }

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
        N64Recomp::FunctionStats stats{};
        if (!N64Recomp::analyze_function(context, func, instructions, stats)) {
            fmt::print(stderr, "Failed to analyze {}\n", func.name);
            output_file.clear();
            return false;
        }

        std::unordered_set<uint32_t> jtbl_lw_instructions{};

        // Add jump table labels into function
        for (const auto& jtbl : stats.jump_tables) {
            jtbl_lw_instructions.insert(jtbl.lw_vram);
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
                generator.emit_goto(fmt::format("skip_{}", num_likely_branches));
            }
            // If there are any other branch labels to insert and we're at the next one, insert it
            if (cur_label != branch_labels.end() && vram >= *cur_label) {
                generator.emit_label(fmt::format("L_{:08X}", *cur_label));
                ++cur_label;
            }

            // Advance the reloc index until we reach the last one or until we get to/pass the current instruction
            while ((reloc_index + 1) < section.relocs.size() && section.relocs[reloc_index].address < vram) {
                reloc_index++;
            }

            // Process the current instruction and check for errors
            if (process_instruction(generator, context, func, stats, jtbl_lw_instructions, instr_index, instructions, output_file, false, needs_link_branch, num_link_branches, reloc_index, needs_link_branch, is_branch_likely, tag_reference_relocs, static_funcs_out) == false) {
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
                fmt::print(output_file, "    ");
                generator.emit_label(fmt::format("skip_{}", num_likely_branches));
                num_likely_branches++;
            }
            // Mark the next instruction as being in a likely delay slot if the 
            in_likely_delay_slot = is_branch_likely;
            // Advance the vram address by the size of one instruction
            vram += 4;
        }
    }

    // Terminate the function
    generator.emit_function_end();
    
    return true;
}

// Wrap the templated function with CGenerator as the template parameter.
bool N64Recomp::recompile_function(const N64Recomp::Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    CGenerator generator{output_file};
    return recompile_function_impl(generator, context, function_index, output_file, static_funcs_out, tag_reference_relocs);
}

bool N64Recomp::recompile_function_custom(Generator& generator, const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    return recompile_function_impl(generator, context, function_index, output_file, static_funcs_out, tag_reference_relocs);
}
