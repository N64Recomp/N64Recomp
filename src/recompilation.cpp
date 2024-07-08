#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "n64recomp.h"
#include "analysis.h"
#include "operations.h"
#include "generator.h"

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
    bool needs_static = false;
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

// Major TODO, this function grew very organically and needs to be cleaned up. Ideally, it'll get split up into some sort of lookup table grouped by similar instruction types.
bool process_instruction(const N64Recomp::Context& context, const N64Recomp::Function& func, const N64Recomp::FunctionStats& stats, const std::unordered_set<uint32_t>& skipped_insns, size_t instr_index, const std::vector<rabbitizer::InstructionCpu>& instructions, std::ofstream& output_file, bool indent, bool emit_link_branch, int link_branch_index, size_t reloc_index, bool& needs_link_branch, bool& is_branch_likely, std::span<std::vector<uint32_t>> static_funcs_out) {
    using namespace N64Recomp;

    const auto& section = context.sections[func.section_index];
    const auto& instr = instructions[instr_index];
    needs_link_branch = false;
    is_branch_likely = false;
    uint32_t instr_vram = instr.getVram();

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
    if (instr.isBranch() || instr.getUniqueId() == InstrId::cpu_j) {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0, fmt::format("L_{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else if (instr.getUniqueId() == InstrId::cpu_jal) {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0, fmt::format("0x{:08X}", (uint32_t)instr.getBranchVramGeneric())));
    } else {
        fmt::print(output_file, "    // 0x{:08X}: {}\n", instr_vram, instr.disassemble(0));
    }

    if (skipped_insns.contains(instr_vram)) {
        return true;
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
        // Only process this relocation if this section is relocatable or if this relocation targets a reference symbol.
        if (section.relocatable || reloc.reference_symbol) {
            // Some symbols are in a nonexistent section (e.g. absolute symbols), so check that the section is valid before doing anything else.
            // Absolute symbols will never need to be relocated so it's safe to skip this.
            // Always process reference symbols relocations.
            if (reloc_section < context.sections.size() || reloc.reference_symbol) {
                // Ignore this reloc if it points to a different section.
                // Also check if the reloc points to the bss section since that will also be relocated with the section.
                // Additionally, always process reference symbol relocations.
                if (reloc_section == func.section_index || reloc_section == section.bss_section_index || reloc.reference_symbol) {
                    // Record the reloc's data.
                    reloc_type = reloc.type;
                    reloc_target_section_offset = reloc.section_offset;
                    // Ignore all relocs that aren't HI16 or LO16.
                    if (reloc_type == N64Recomp::RelocType::R_MIPS_HI16 || reloc_type == N64Recomp::RelocType::R_MIPS_LO16 || reloc_type == N64Recomp::RelocType::R_MIPS_26) {
                        if (reloc.reference_symbol) {
                            reloc_reference_symbol = reloc.symbol_index;
                            static N64Recomp::ReferenceSection dummy_section{
                                .rom_addr = 0,
                                .ram_addr = 0,
                                .size = 0,
                                .relocatable = false
                            };
                            const auto& reloc_reference_section = reloc.target_section == N64Recomp::SectionAbsolute ? dummy_section : context.reference_sections[reloc.target_section];
                            // Resolve HI16 and LO16 reference symbol relocs to non-relocatable sections by patching the instruction immediate.
                            if (!reloc_reference_section.relocatable && (reloc_type == N64Recomp::RelocType::R_MIPS_HI16 || reloc_type == N64Recomp::RelocType::R_MIPS_LO16)) {
                                uint32_t full_immediate = reloc.section_offset + reloc_reference_section.ram_addr;
                                
                                if (reloc_type == N64Recomp::RelocType::R_MIPS_HI16) {
                                    imm = (full_immediate >> 16) + ((full_immediate >> 15) & 1);
                                    reloc_type = N64Recomp::RelocType::R_MIPS_NONE;
                                }
                                else if (reloc_type == N64Recomp::RelocType::R_MIPS_LO16) {
                                    imm = full_immediate & 0xFFFF;
                                    reloc_type = N64Recomp::RelocType::R_MIPS_NONE;
                                }
                                
                                // The reloc has been processed, so delete it to none to prevent it getting processed a second time during instruction code generation.
                                reloc_type = N64Recomp::RelocType::R_MIPS_NONE;
                                reloc_reference_symbol = (size_t)-1;
                            }
                        }
                    }

                    // Repoint bss relocations at their non-bss counterpart section.
                    if (reloc_section == section.bss_section_index) {
                        reloc_section = func.section_index;
                    }
                }
            }
        }
    }

    auto print_line = [&]<typename... Ts>(fmt::format_string<Ts...> fmt_str, Ts ...args) {
        print_indent();
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        fmt::print(output_file, ";\n");
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
            if (!process_instruction(context, func, stats, skipped_insns, instr_index + 1, instructions, output_file, false, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out)) {
                return false;
            }
        }
        print_indent();
        fmt::vprint(output_file, fmt_str, fmt::make_format_args(args...));
        if (needs_link_branch) {
            fmt::print(output_file, ";\n    goto after_{};\n", link_branch_index);
        } else {
            fmt::print(output_file, ";\n");
        }
        return true;
    };

    auto print_func_call = [reloc_target_section_offset, reloc_reference_symbol, reloc_type, &context, &section, &func, &static_funcs_out, &needs_link_branch, &print_unconditional_branch]
        (uint32_t target_func_vram, bool link_branch = true, bool indent = false)
    {
        std::string jal_target_name{};
        if (reloc_reference_symbol != (size_t)-1) {
            const auto& ref_symbol = context.reference_symbols[reloc_reference_symbol];
            const std::string& ref_symbol_name = context.reference_symbol_names[reloc_reference_symbol];

            if (reloc_type != N64Recomp::RelocType::R_MIPS_26) {
                fmt::print(stderr, "Unsupported reloc type {} on jal instruction in {}\n", (int)reloc_type, func.name);
                return false;
            }

            if (ref_symbol.section_offset != reloc_target_section_offset) {
                fmt::print(stderr, "Function {} uses a MIPS_R_26 addend, which is not supported yet\n", func.name);
                return false;
            }

            jal_target_name = ref_symbol_name;
        }
        else {
            size_t matched_func_index = 0;
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
                    break;
                case JalResolutionResult::Ambiguous:
                    fmt::print(stderr, "[Info] Ambiguous jal target 0x{:08X} in function {}, falling back to function lookup\n", target_func_vram, func.name);
                    // Relocation isn't necessary for jumps inside a relocatable section, as this code path will never run if the target vram
                    // is in the current function's section (see the branch for `in_current_section` above).
                    // If a game ever needs to jump between multiple relocatable sections, relocation will be necessary here.
                    jal_target_name = fmt::format("LOOKUP_FUNC(0x{:08X})", target_func_vram);
                    break;
                case JalResolutionResult::Error:
                    fmt::print(stderr, "Internal error when resolving jal to address 0x{:08X} in function {}\n", target_func_vram, func.name);
                    return false;
            }
        }
        needs_link_branch = link_branch;
        if (indent) {
            print_unconditional_branch("    {}(rdram, ctx)", jal_target_name);
        } else {
            print_unconditional_branch("{}(rdram, ctx)", jal_target_name);
        }
        return true;
    };

    auto print_branch = [&](uint32_t branch_target) {
        if (branch_target < func.vram || branch_target >= func_vram_end) {
            // FIXME: how to deal with static functions?
            if (context.functions_by_vram.find(branch_target) != context.functions_by_vram.end()) {
                fmt::print("Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                if (!print_func_call(branch_target, false, true)) {
                    return false;
                }
                print_line("    return");
                fmt::print(output_file, "    }}\n");
                return true;
            }

            fmt::print(stderr, "[Warn] Function {} is branching outside of the function (to 0x{:08X})\n", func.name, branch_target);
        }

        if (instr_index < instructions.size() - 1) {
            bool dummy_needs_link_branch;
            bool dummy_is_branch_likely;
            size_t next_reloc_index = reloc_index;
            uint32_t next_vram = instr_vram + 4;
            if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                next_reloc_index++;
            }
            if (!process_instruction(context, func, stats, skipped_insns, instr_index + 1, instructions, output_file, true, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out)) {
                return false;
            }
        }

        fmt::print(output_file, "        goto L_{:08X};\n", branch_target);
        if (needs_link_branch) {
            fmt::print(output_file, "        goto after_{};\n", link_branch_index);
        }
        return true;
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

    bool handled = true;

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
                print_line("gpr jr_addend_{:08X} = {}{}", cur_jtbl.jr_vram, ctx_gpr_prefix(cur_jtbl.addend_reg), cur_jtbl.addend_reg);
            }
        }
        break;
    case InstrId::cpu_mult:
        print_line("result = S64(S32({}{})) * S64(S32({}{})); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_dmult:
        print_line("DMULT(S64({}{}), S64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_multu:
        print_line("result = U64(U32({}{})) * U64(U32({}{})); lo = S32(result >> 0); hi = S32(result >> 32)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_dmultu:
        print_line("DMULTU(U64({}{}), U64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_div:
        // Cast to 64-bits before division to prevent artihmetic exception for s32(0x80000000) / -1
        print_line("lo = S32(S64(S32({}{})) / S64(S32({}{}))); hi = S32(S64(S32({}{})) % S64(S32({}{})))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_ddiv:
        print_line("DDIV(S64({}{}), S64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_divu:
        print_line("lo = S32(U32({}{}) / U32({}{})); hi = S32(U32({}{}) % U32({}{}))", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt, ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    case InstrId::cpu_ddivu:
        print_line("DDIVU(U64({}{}), U64({}{}), &lo, &hi)", ctx_gpr_prefix(rs), rs, ctx_gpr_prefix(rt), rt);
        break;
    // Branches
    case InstrId::cpu_jal:
        if (!print_func_call(instr.getBranchVramGeneric())) {
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
        print_unconditional_branch("LOOKUP_FUNC({}{})(rdram, ctx)", ctx_gpr_prefix(rs), rs);
        break;
    case InstrId::cpu_j:
    case InstrId::cpu_b:
        {
            uint32_t branch_target = instr.getBranchVramGeneric();
            if (branch_target == instr_vram) {
                print_line("pause_self(rdram)");
            }
            // Check if the branch is within this function
            else if (branch_target >= func.vram && branch_target < func_vram_end) {
                print_unconditional_branch("goto L_{:08X}", branch_target);
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
                fmt::print("Tail call in {} to 0x{:08X}\n", func.name, branch_target);
                if (!print_func_call(branch_target, false)) {
                    return false;
                }
                print_line("return");
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
                [instr_vram](const N64Recomp::JumpTable& jtbl) {
                    return jtbl.jr_vram == instr_vram;
                });

            if (jtbl_find_result != stats.jump_tables.end()) {
                const N64Recomp::JumpTable& cur_jtbl = *jtbl_find_result;
                bool dummy_needs_link_branch, dummy_is_branch_likely;
                size_t next_reloc_index = reloc_index;
                uint32_t next_vram = instr_vram + 4;
                if (reloc_index + 1 < section.relocs.size() && next_vram > section.relocs[reloc_index].address) {
                    next_reloc_index++;
                }
                if (!process_instruction(context, func, stats, skipped_insns, instr_index + 1, instructions, output_file, false, false, link_branch_index, next_reloc_index, dummy_needs_link_branch, dummy_is_branch_likely, static_funcs_out)) {
                    return false;
                }
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
                [instr_vram](const N64Recomp::AbsoluteJump& jump) {
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
    case InstrId::cpu_break:
        print_line("do_break({})", instr_vram);
        break;

    // Cop1 rounding mode
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
    default:
        handled = false;
        break;
    }

    CGenerator generator{};
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
    instruction_context.reloc_type = reloc_type;
    instruction_context.reloc_section_index = reloc_section;
    instruction_context.reloc_target_section_offset = reloc_target_section_offset;
    
    auto do_check_fr = [](std::ostream& output_file, const CGenerator& generator, const InstructionContext& ctx, Operand operand) {
        switch (operand) {
            case Operand::Fd:
            case Operand::FdDouble:
            case Operand::FdU32L:
            case Operand::FdU32H:
            case Operand::FdU64:
                generator.emit_check_fr(output_file, ctx.fd);
                break;
            case Operand::Fs:
            case Operand::FsDouble:
            case Operand::FsU32L:
            case Operand::FsU32H:
            case Operand::FsU64:
                generator.emit_check_fr(output_file, ctx.fs);
                break;
            case Operand::Ft:
            case Operand::FtDouble:
            case Operand::FtU32L:
            case Operand::FtU32H:
            case Operand::FtU64:
                generator.emit_check_fr(output_file, ctx.ft);
                break;
            default:
                // No MIPS3 float check needed for non-float operands.
                break;
        }
    };
    
    auto do_check_nan = [](std::ostream& output_file, const CGenerator& generator, const InstructionContext& ctx, Operand operand) {
        switch (operand) {
            case Operand::Fd:
                generator.emit_check_nan(output_file, ctx.fd, false);
                break;
            case Operand::Fs:
                generator.emit_check_nan(output_file, ctx.fs, false);
                break;
            case Operand::Ft:
                generator.emit_check_nan(output_file, ctx.ft, false);
                break;
            case Operand::FdDouble:
                generator.emit_check_nan(output_file, ctx.fd, true);
                break;
            case Operand::FsDouble:
                generator.emit_check_nan(output_file, ctx.fs, true);
                break;
            case Operand::FtDouble:
                generator.emit_check_nan(output_file, ctx.ft, true);
                break;
            default:
                // No NaN checks needed for non-float operands.
                break;
        }
    };

    auto find_binary_it = binary_ops.find(instr.getUniqueId());
    if (find_binary_it != binary_ops.end()) {
        print_indent();
        const BinaryOp& op = find_binary_it->second;
        
        if (op.check_fr) {
            do_check_fr(output_file, generator, instruction_context, op.output);
            do_check_fr(output_file, generator, instruction_context, op.operands.operands[0]);
            do_check_fr(output_file, generator, instruction_context, op.operands.operands[1]);
        }

        if (op.check_nan) {
            do_check_nan(output_file, generator, instruction_context, op.operands.operands[0]);
            do_check_nan(output_file, generator, instruction_context, op.operands.operands[1]);
            fmt::print(output_file, "\n    ");
        }

        generator.process_binary_op(output_file, op, instruction_context);
        handled = true;
    }

    auto find_unary_it = unary_ops.find(instr.getUniqueId());
    if (find_unary_it != unary_ops.end()) {
        print_indent();
        const UnaryOp& op = find_unary_it->second;
        
        if (op.check_fr) {
            do_check_fr(output_file, generator, instruction_context, op.output);
            do_check_fr(output_file, generator, instruction_context, op.input);
        }

        if (op.check_nan) {
            do_check_nan(output_file, generator, instruction_context, op.input);
            fmt::print(output_file, "\n    ");
        }

        generator.process_unary_op(output_file, op, instruction_context);
        handled = true;
    }

    auto find_conditional_branch_it = conditional_branch_ops.find(instr.getUniqueId());
    if (find_conditional_branch_it != conditional_branch_ops.end()) {
        print_indent();
        generator.emit_branch_condition(output_file, find_conditional_branch_it->second, instruction_context);

        print_indent();
        if (find_conditional_branch_it->second.link) {
            if (!print_func_call(instr.getBranchVramGeneric())) {
                return false;
            }
        }
        else {
            if (!print_branch((uint32_t)instr.getBranchVramGeneric())) {
                return false;
            }
        }

        generator.emit_branch_close(output_file);
        
        is_branch_likely = find_conditional_branch_it->second.likely;
        handled = true;
    }

    auto find_store_it = store_ops.find(instr.getUniqueId());
    if (find_store_it != store_ops.end()) {
        print_indent();
        const StoreOp& op = find_store_it->second;

        if (op.type == StoreOpType::SDC1) {
            do_check_fr(output_file, generator, instruction_context, op.value_input);
        }

        generator.process_store_op(output_file, op, instruction_context);
        handled = true;
    }

    if (!handled) {
        fmt::print(stderr, "Unhandled instruction: {}\n", instr.getOpcodeName());
        return false;
    }

    // TODO is this used?
    if (emit_link_branch) {
        fmt::print(output_file, "    after_{}:\n", link_branch_index);
    }

    return true;
}

bool N64Recomp::recompile_function(const N64Recomp::Context& context, const N64Recomp::Function& func, const std::string& recomp_include, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool write_header) {
    //fmt::print("Recompiling {}\n", func.name);
    std::vector<rabbitizer::InstructionCpu> instructions;

    if (write_header) {
        // Write the file header
        fmt::print(output_file,
            "{}\n"
            "\n",
            recomp_include);
    }

    fmt::print(output_file,
        "RECOMP_FUNC void {}(uint8_t* rdram, recomp_context* ctx) {{\n"
        // these variables shouldn't need to be preserved across function boundaries, so make them local for more efficient output
        "    uint64_t hi = 0, lo = 0, result = 0;\n"
        "    unsigned int rounding_mode = DEFAULT_ROUNDING_MODE;\n"
        "    int c1cs = 0;\n", // cop1 conditional signal
        func.name);

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

            // Advance the reloc index until we reach the last one or until we get to/pass the current instruction
            while ((reloc_index + 1) < section.relocs.size() && section.relocs[reloc_index].address < vram) {
                reloc_index++;
            }

            // Process the current instruction and check for errors
            if (process_instruction(context, func, stats, skipped_insns, instr_index, instructions, output_file, false, needs_link_branch, num_link_branches, reloc_index, needs_link_branch, is_branch_likely, static_funcs_out) == false) {
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
    
    return true;
}
