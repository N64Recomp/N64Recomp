#include <set>
#include <algorithm>

#include "rabbitizer.hpp"
#include "fmt/format.h"

#include "n64recomp.h"
#include "analysis.h"

extern "C" const char* RabbitizerRegister_getNameGpr(uint8_t regValue);

// If 64-bit addressing is ever implemented, these will need to be changed to 64-bit values
struct RegState {
    // For tracking a register that will be used to load from RAM
    uint32_t prev_lui;
    uint32_t prev_addiu_vram;
    uint32_t prev_addu_vram;
    uint8_t prev_addend_reg;
    uint32_t prev_got_offset; // offset of lw rt,offset(gp)
    bool valid_lui;
    bool valid_addiu;
    bool valid_addend;
    bool valid_got_offset;
    // For tracking a register that has been loaded from RAM
    uint32_t loaded_lw_vram;
    uint32_t loaded_addu_vram;
    uint32_t loaded_address;
    uint8_t loaded_addend_reg;
    bool valid_loaded;
    bool valid_got_loaded; // valid load through the GOT

    RegState() = default;

    void invalidate() {
        prev_lui = 0;
        prev_addiu_vram = 0;
        prev_addu_vram = 0;
        prev_addend_reg = 0;
        prev_got_offset = 0;

        valid_lui = false;
        valid_addiu = false;
        valid_addend = false;
        valid_got_offset = false;

        loaded_lw_vram = 0;
        loaded_addu_vram = 0;
        loaded_address = 0;
        loaded_addend_reg = 0;

        valid_loaded = false;
        valid_got_loaded = false;
    }
};

using InstrId = rabbitizer::InstrId::UniqueId;
using RegId = rabbitizer::Registers::Cpu::GprO32;

bool analyze_instruction(const rabbitizer::InstructionCpu& instr, const N64Recomp::Function& func, N64Recomp::FunctionStats& stats,
    RegState reg_states[32], std::vector<RegState>& stack_states, bool is_gp_reg_defined) {
    // Temporary register state for tracking the register being operated on
    RegState temp{};

    int rd = (int)instr.GetO32_rd();
    int rs = (int)instr.GetO32_rs();
    int base = rs;
    int rt = (int)instr.GetO32_rt();
    int sa = (int)instr.Get_sa();

    uint16_t imm = instr.Get_immediate();

    auto check_move = [&]() {
        if (rs == 0) {
            // rs is zero so copy rt to rd
            reg_states[rd] = reg_states[rt];
        } else if (rt == 0) {
            // rt is zero so copy rs to rd
            reg_states[rd] = reg_states[rs];
        } else {
            // Not a move, invalidate rd
            reg_states[rd].invalidate();
        }
    };

    switch (instr.getUniqueId()) {
    case InstrId::cpu_lui:
        // rt has been completely overwritten, so invalidate it
        reg_states[rt].invalidate();
        reg_states[rt].prev_lui = (int16_t)imm << 16;
        reg_states[rt].valid_lui = true;
        break;
    case InstrId::cpu_addiu:
        // The target reg is a copy of the source reg plus an immediate, so copy the source reg's state
        reg_states[rt] = reg_states[rs];
        // Set the addiu state if and only if there hasn't been an addiu already
        if (!reg_states[rt].valid_addiu) {
            reg_states[rt].prev_addiu_vram = (int16_t)imm;
            reg_states[rt].valid_addiu = true;
        } else {
            // Otherwise, there have been 2 or more consecutive addius so invalidate the whole register
            reg_states[rt].invalidate();
        }
        break;
    case InstrId::cpu_addu:
        // rd has been completely overwritten, so invalidate it
        temp.invalidate();
        if (reg_states[rs].valid_got_offset != reg_states[rt].valid_got_offset) {
            // Track which of the two registers has the valid GOT offset state and which is the addend
            int valid_got_offset_reg = reg_states[rs].valid_got_offset ? rs : rt;
            int addend_reg = reg_states[rs].valid_got_offset ? rt : rs;

            // Copy the got offset reg's state into the destination reg, then set the destination reg's addend to the other operand
            temp = reg_states[valid_got_offset_reg];
            temp.valid_addend = true;
            temp.prev_addend_reg = addend_reg;
            temp.prev_addu_vram = instr.getVram();
        } else if (((rs == (int)RegId::GPR_O32_gp) || (rt == (int)RegId::GPR_O32_gp)) 
                && reg_states[rs].valid_got_loaded != reg_states[rt].valid_got_loaded) {
            // `addu rd, rs, $gp` or `addu rd, $gp, rt` after valid GOT load, this is the last part of a position independent
            // jump table call. Keep the register state intact.
            int valid_gp_loaded_reg = reg_states[rs].valid_got_loaded ? rs : rt;

            temp = reg_states[valid_gp_loaded_reg];
        }
        // Exactly one of the two addend register states should have a valid lui at this time
        else if (reg_states[rs].valid_lui != reg_states[rt].valid_lui) {
            // Track which of the two registers has the valid lui state and which is the addend
            int valid_lui_reg = reg_states[rs].valid_lui ? rs : rt;
            int addend_reg = reg_states[rs].valid_lui ? rt : rs;

            // Copy the lui reg's state into the destination reg, then set the destination reg's addend to the other operand
            temp = reg_states[valid_lui_reg];
            temp.valid_addend = true;
            temp.prev_addend_reg = addend_reg;
            temp.prev_addu_vram = instr.getVram();
        } else {
            // Check if this is a move
            check_move();
        }
        reg_states[rd] = temp;
        break;
    case InstrId::cpu_daddu:
    case InstrId::cpu_or:
        check_move();
        break;
    case InstrId::cpu_sw:
        // If this is a store to the stack, copy the state of rt into the stack at the given offset
        if (base == (int)RegId::GPR_O32_sp) {
            if ((imm & 0b11) != 0) {
                fmt::print(stderr, "Invalid alignment on offset for sw to stack: {}\n", (int16_t)imm);
                return false;
            }
            if (((int16_t)imm) < 0) {
                fmt::print(stderr, "Negative offset for sw to stack: {}\n", (int16_t)imm);
                return false;
            }
            size_t stack_offset = imm / 4;
            if (stack_offset >= stack_states.size()) {
                stack_states.resize(stack_offset + 1);
            }
            stack_states[stack_offset] = reg_states[rt];
        }
        break;
    case InstrId::cpu_lw:
        // rt has been completely overwritten, so invalidate it
        temp.invalidate();
        // If this is a load from the stack, copy the state of the stack at the given offset to rt
        if (base == (int)RegId::GPR_O32_sp) {
            if ((imm & 0b11) != 0) {
                fmt::print(stderr, "Invalid alignment on offset for lw from stack: {}\n", (int16_t)imm);
                return false;
            }
            if (((int16_t)imm) < 0) {
                fmt::print(stderr, "Negative offset for lw from stack: {}\n", (int16_t)imm);
                return false;
            }
            size_t stack_offset = imm / 4;
            if (stack_offset >= stack_states.size()) {
                stack_states.resize(stack_offset + 1);
            }
            temp = stack_states[stack_offset];
        }
        // If the base register has a valid lui state and a valid addend before this, then this may be a load from a jump table
        else if (reg_states[base].valid_lui && reg_states[base].valid_addend) {
            // Exactly one of the lw and the base reg should have a valid lo16 value. However, the lo16 may end up just being zero by pure luck,
            // so allow the case where the lo16 immediate is zero and the register state doesn't have a valid addiu immediate.
            // This means the only invalid case is where they're both true.
            bool nonzero_immediate = imm != 0;
            if (!(nonzero_immediate && reg_states[base].valid_addiu)) {
                uint32_t lo16;
                if (nonzero_immediate) {
                    lo16 = (int16_t)imm;
                } else {
                    lo16 = reg_states[base].prev_addiu_vram;
                }

                uint32_t address = reg_states[base].prev_lui + lo16;
                temp.valid_loaded = true;
                temp.loaded_lw_vram = instr.getVram();
                temp.loaded_address = address;
                temp.loaded_addend_reg = reg_states[base].prev_addend_reg;
                temp.loaded_addu_vram = reg_states[base].prev_addu_vram;
            }
        }
        // If the base register has a valid GOT offset and a valid addend before this, then this may be a load from a position independent jump table
        else if (reg_states[base].valid_got_offset && reg_states[base].valid_addend) {
            // At this point, we will have the offset from the value of the previously read GOT entry to the address being
            // loaded here as well as the GOT entry offset itself
            temp.valid_got_loaded = true;
            temp.loaded_lw_vram = instr.getVram();
            temp.loaded_address = imm; // This address is relative for now, we'll calculate the absolute address later
            temp.loaded_addend_reg = reg_states[base].prev_addend_reg;
            temp.loaded_addu_vram = reg_states[base].prev_addu_vram;
            temp.prev_got_offset = reg_states[base].prev_got_offset;
        } else if (base == (int)RegId::GPR_O32_gp) {
            // lw from the $gp register implies a read from the global offset table
            if (!is_gp_reg_defined) {
                fmt::print(stderr, "Found $gp register usage in section without a defined $gp value at 0x{:08X} in {}\n", 
                    instr.getVram(), func.name);
                return false;
            }
            temp.prev_got_offset = imm;
            temp.valid_got_offset = true;
        }
        reg_states[rt] = temp;
        break;
    case InstrId::cpu_jr:
        // Ignore jr $ra
        if (rs == (int)rabbitizer::Registers::Cpu::GprO32::GPR_O32_ra) {
            break;
        }
        // Check if the source reg has a valid loaded state and if so record that as a jump table
        if (reg_states[rs].valid_loaded) {
            stats.jump_tables.emplace_back(
                reg_states[rs].loaded_address,
                reg_states[rs].loaded_addend_reg,
                0,
                reg_states[rs].loaded_lw_vram,
                reg_states[rs].loaded_addu_vram,
                instr.getVram(),
                std::nullopt,
                std::vector<uint32_t>{}
            );
        } else if (reg_states[rs].valid_got_loaded) {
            stats.jump_tables.emplace_back(
                reg_states[rs].loaded_address,
                reg_states[rs].loaded_addend_reg,
                0,
                reg_states[rs].loaded_lw_vram,
                reg_states[rs].loaded_addu_vram,
                instr.getVram(),
                reg_states[rs].prev_got_offset,
                std::vector<uint32_t>{}
            );
        } else if (reg_states[rs].valid_lui && reg_states[rs].valid_addiu && !reg_states[rs].valid_addend && !reg_states[rs].valid_loaded) {
            uint32_t address = reg_states[rs].prev_addiu_vram + reg_states[rs].prev_lui;
            stats.absolute_jumps.emplace_back(
                address,
                instr.getVram()
            );
        }
        // Allow tail calls (TODO account for trailing nops due to bad function splits)
        else if (instr.getVram() != func.vram + (func.words.size() - 2) * sizeof(func.words[0])) {
            // Inconclusive analysis
            fmt::print(stderr, "Failed to to find jump table for `jr {}` at 0x{:08X} in {}\n", RabbitizerRegister_getNameGpr(rs), instr.getVram(), func.name);
            return false;
        }
        break;
    default:
        if (instr.modifiesRd()) {
            reg_states[rd].invalidate();
        }
        if (instr.modifiesRt()) {
            reg_states[rt].invalidate();
        }
        break;
    }
    return true;
}

bool N64Recomp::analyze_function(const N64Recomp::Context& context, const N64Recomp::Function& func,
    const std::vector<rabbitizer::InstructionCpu>& instructions, N64Recomp::FunctionStats& stats) {
    const Section* section = &context.sections[func.section_index];
    std::optional<uint32_t> gp_ram_addr = section->gp_ram_addr;

    // Create a state to track each register (r0 won't be used)
    RegState reg_states[32] {};
    std::vector<RegState> stack_states{};

    // Look for jump tables
    // A linear search through the func won't be accurate due to not taking control flow into account, but it'll work for finding jtables
    for (const auto& instr : instructions) {
        if (!analyze_instruction(instr, func, stats, reg_states, stack_states, gp_ram_addr.has_value())) {
            return false;
        }
    }

    // Calculate absolute addresses for position-independent jump tables
    if (gp_ram_addr.has_value()) {
        uint32_t gp_rom_addr = gp_ram_addr.value() + func.rom - func.vram;

        for (size_t i = 0; i < stats.jump_tables.size(); i++) {
            JumpTable& cur_jtbl = stats.jump_tables[i];

            if (cur_jtbl.got_offset.has_value()) {
                uint32_t got_word = byteswap(*reinterpret_cast<const uint32_t*>(&context.rom[gp_rom_addr + cur_jtbl.got_offset.value()]));

                cur_jtbl.vram += (section->ram_addr + got_word);
            }
        }
    }

    // Sort jump tables by their address
    std::sort(stats.jump_tables.begin(), stats.jump_tables.end(),
        [](const JumpTable& a, const JumpTable& b)
    {
        return a.vram < b.vram;
    });

    // Determine jump table sizes
    for (size_t i = 0; i < stats.jump_tables.size(); i++) {
        JumpTable& cur_jtbl = stats.jump_tables[i];
        uint32_t end_address = (uint32_t)-1;
        uint32_t entry_count = 0;
        uint32_t vram = cur_jtbl.vram;

        if (i < stats.jump_tables.size() - 1) {
            end_address = stats.jump_tables[i + 1].vram;
        }

        // TODO this assumes that the jump table is in the same section as the function itself
        cur_jtbl.rom = cur_jtbl.vram + func.rom - func.vram;

        while (vram < end_address) {
            // Retrieve the current entry of the jump table
            // TODO same as above
            uint32_t rom_addr = vram + func.rom - func.vram;
            uint32_t jtbl_word = byteswap(*reinterpret_cast<const uint32_t*>(&context.rom[rom_addr]));

            if (cur_jtbl.got_offset.has_value() && gp_ram_addr.has_value()) {
                // Position independent jump tables have values that are offsets from $gp,
                // convert those to absolute addresses
                jtbl_word += gp_ram_addr.value();
            }

            // Check if the entry is a valid address in the current function
            if (jtbl_word < func.vram || jtbl_word >= func.vram + func.words.size() * sizeof(func.words[0])) {
                // If it's not then this is the end of the jump table
                break;
            }
            cur_jtbl.entries.push_back(jtbl_word);
            vram += 4;
        }

        if (cur_jtbl.entries.size() == 0) {
            fmt::print("Failed to determine size of jump table at 0x{:08X} for instruction at 0x{:08X}\n", cur_jtbl.vram, cur_jtbl.jr_vram);
            return false;
        }

        //fmt::print("Jtbl at 0x{:08X} (rom 0x{:08X}) with {} entries used by instr at 0x{:08X}\n", cur_jtbl.vram, cur_jtbl.rom, cur_jtbl.entries.size(), cur_jtbl.jr_vram);
    }

    return true;
}
