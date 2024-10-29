#ifndef __RECOMP_ANALYSIS_H__
#define __RECOMP_ANALYSIS_H__

#include <cstdint>
#include <vector>

#include "n64recomp.h"

namespace N64Recomp {
    struct JumpTable {
        uint32_t vram;
        uint32_t addend_reg;
        uint32_t rom;
        uint32_t lw_vram;
        uint32_t addu_vram;
        uint32_t jr_vram;
        std::optional<uint32_t> got_offset;
        std::vector<uint32_t> entries;

        JumpTable(uint32_t vram, uint32_t addend_reg, uint32_t rom, uint32_t lw_vram, uint32_t addu_vram, uint32_t jr_vram, std::optional<uint32_t> got_offset, std::vector<uint32_t>&& entries)
                : vram(vram), addend_reg(addend_reg), rom(rom), lw_vram(lw_vram), addu_vram(addu_vram), jr_vram(jr_vram), got_offset(got_offset), entries(std::move(entries)) {}
    };

    struct AbsoluteJump {
        uint32_t jump_target;
        uint32_t instruction_vram;

        AbsoluteJump(uint32_t jump_target, uint32_t instruction_vram) : jump_target(jump_target), instruction_vram(instruction_vram) {}
    };

    struct FunctionStats {
        std::vector<JumpTable> jump_tables;
        std::vector<AbsoluteJump> absolute_jumps;
    };

    bool analyze_function(const Context& context, const Function& function, const std::vector<rabbitizer::InstructionCpu>& instructions, FunctionStats& stats);
}

#endif