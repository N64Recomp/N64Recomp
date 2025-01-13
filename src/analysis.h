#ifndef __RECOMP_ANALYSIS_H__
#define __RECOMP_ANALYSIS_H__

#include <cstdint>
#include <vector>

#include "recompiler/context.h"

namespace N64Recomp {
    struct AbsoluteJump {
        uint32_t jump_target;
        uint32_t instruction_vram;

        AbsoluteJump(uint32_t jump_target, uint32_t instruction_vram) : jump_target(jump_target), instruction_vram(instruction_vram) {}
    };

    struct FunctionStats {
        std::vector<JumpTable> jump_tables;
    };

    bool analyze_function(const Context& context, const Function& function, const std::vector<rabbitizer::InstructionCpu>& instructions, FunctionStats& stats);
}

#endif