#ifndef __RECOMP_ANALYSIS_H__
#define __RECOMP_ANALYSIS_H__

#include <cstdint>
#include <string>
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

    // Discover the byte-size of a function whose entry sits at
    // `entry_offset` within `body`. Performs a BFS-based control-flow
    // walk that follows conditional branches (target + fall-through),
    // unconditional j/jal targets when intra-body, jr $ra returns,
    // and jr-via-jump-table dispatches (resolved by the existing
    // register-state simulator from analyze_function).
    //
    // `body` is the raw decompressed bytes of the section's body in
    // big-endian instruction layout (same shape as Function::words but
    // as a byte buffer; bytes_size is the upper bound).
    //
    // `vram_base` is the link-time vram of body[0] — used to translate
    // branch/jal targets back to body offsets.
    //
    // On success, sets `size_out` to the function's byte size (always
    // a multiple of 4) and returns true.
    //
    // On failure, populates `error_out` with a specific message
    // identifying the offending instruction or jump-table issue, and
    // returns false. Per the project's no-stubs principle, callers
    // should treat false as a build-time error, NOT a graceful skip.
    bool discover_function_bounds(
        const uint8_t* body, size_t bytes_size,
        uint32_t vram_base, uint32_t entry_offset,
        size_t& size_out, std::string& error_out);
}

#endif