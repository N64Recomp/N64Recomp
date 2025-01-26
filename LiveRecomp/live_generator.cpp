#include <cassert>
#include <fstream>
#include <unordered_map>
#include <cmath>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/live_recompiler.h"
#include "recomp.h"

#include "sljitLir.h"

static_assert(sizeof(void*) >= sizeof(sljit_uw), "`void*` must be able to hold a `sljit_uw` value for rewritable jumps!");

constexpr uint64_t rdram_offset = 0xFFFFFFFF80000000ULL;

void N64Recomp::live_recompiler_init() {
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;
}

namespace Registers {
    constexpr int rdram = SLJIT_S0; // stores (rdram - rdram_offset)
    constexpr int ctx = SLJIT_S1; // stores ctx
    constexpr int c1cs = SLJIT_S2; // stores ctx
    constexpr int hi = SLJIT_S3; // stores ctx
    constexpr int lo = SLJIT_S4; // stores ctx
    constexpr int arithmetic_temp1 = SLJIT_R0;
    constexpr int arithmetic_temp2 = SLJIT_R1;
    constexpr int arithmetic_temp3 = SLJIT_R2;
    constexpr int arithmetic_temp4 = SLJIT_R3;
}

struct InnerCall {
    size_t target_func_index;
    sljit_jump* jump;
};

struct ReferenceSymbolCall {
    N64Recomp::SymbolReference reference;
    sljit_jump* jump;
};

struct SwitchErrorJump {
    uint32_t instr_vram;
    uint32_t jtbl_vram;
    sljit_jump* jump;
};

struct N64Recomp::LiveGeneratorContext {
    std::string function_name;
    std::unordered_map<std::string, sljit_label*> labels;
    std::unordered_map<std::string, std::vector<sljit_jump*>> pending_jumps;
    std::vector<sljit_label*> func_labels;
    std::vector<InnerCall> inner_calls;
    std::vector<std::vector<std::string>> switch_jump_labels;
    // See LiveGeneratorOutput::jump_tables for info. Contains sljit labels so they can be linked after recompilation.
    std::vector<std::pair<std::vector<sljit_label*>, std::unique_ptr<void*[]>>> unlinked_jump_tables;
    // Jump tables for the current function being recompiled.
    std::vector<std::unique_ptr<void*[]>> pending_jump_tables;
    // See LiveGeneratorOutput::reference_symbol_jumps for info.
    std::vector<std::pair<ReferenceJumpDetails, sljit_jump*>> reference_symbol_jumps;
    // See LiveGeneratorOutput::import_jumps_by_index for info.
    std::unordered_multimap<size_t, sljit_jump*> import_jumps_by_index;
    std::vector<SwitchErrorJump> switch_error_jumps;
    sljit_jump* cur_branch_jump;
};

N64Recomp::LiveGenerator::LiveGenerator(size_t num_funcs, const LiveGeneratorInputs& inputs) : inputs(inputs) {
    compiler = sljit_create_compiler(nullptr);
    context = std::make_unique<LiveGeneratorContext>();
    context->func_labels.resize(num_funcs);
    errored = false;
}

N64Recomp::LiveGenerator::~LiveGenerator() {
    if (compiler != nullptr) {
        sljit_free_compiler(compiler);
        compiler = nullptr;
    }
}

N64Recomp::LiveGeneratorOutput N64Recomp::LiveGenerator::finish() {
    LiveGeneratorOutput ret{};
    if (errored) {
        ret.good = false;
        return ret;
    }
    
    ret.good = true;

    // Populate all the pending inner function calls.
    for (const InnerCall& call : context->inner_calls) {
        sljit_label* target_func_label = context->func_labels[call.target_func_index];

        // Generation isn't valid if the target function wasn't recompiled.
        if (target_func_label == nullptr) {
            return { };
        }

        sljit_set_label(call.jump, target_func_label);
    }

    // Generate the switch error jump targets and assign the jump labels.
    if (!context->switch_error_jumps.empty()) {
        // Allocate the function name and place it in the literals.
        char* func_name = new char[context->function_name.size() + 1];
        memcpy(func_name, context->function_name.c_str(), context->function_name.size());
        func_name[context->function_name.size()] = '\x00';
        ret.string_literals.emplace_back(func_name);

        std::vector<sljit_jump*> switch_error_return_jumps{};
        switch_error_return_jumps.resize(context->switch_error_jumps.size());

        // Generate and assign the labels for the switch error jumps.
        for (size_t i = 0; i < context->switch_error_jumps.size(); i++) {
            const auto& cur_error_jump = context->switch_error_jumps[i];

            // Generate a label and assign it to the jump.
            sljit_set_label(cur_error_jump.jump, sljit_emit_label(compiler));

            // Load the arguments (function name, vram, jump table address)
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, sljit_sw(func_name));
            sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, sljit_sw(cur_error_jump.instr_vram));
            sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R2, 0, SLJIT_IMM, sljit_sw(cur_error_jump.jtbl_vram));
            
            // Call switch_error.
            sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, 32, 32), SLJIT_IMM, sljit_sw(inputs.switch_error));

            // Jump to the return statement.
            switch_error_return_jumps[i] = sljit_emit_jump(compiler, SLJIT_JUMP);
        }

        // Generate the return statement.
        sljit_label* return_label = sljit_emit_label(compiler);
        sljit_emit_return_void(compiler);

        // Assign the label for all the return jumps.
        for (sljit_jump* cur_jump : switch_error_return_jumps) {
            sljit_set_label(cur_jump, return_label);
        }
    }
    context->switch_error_jumps.clear();

    // Generate the code.
    ret.code = sljit_generate_code(compiler, 0, NULL);
    ret.code_size = sljit_get_generated_code_size(compiler);
    ret.functions.resize(context->func_labels.size());

    // Get the function addresses.
    for (size_t func_index = 0; func_index < ret.functions.size(); func_index++) {
        sljit_label* func_label = context->func_labels[func_index];

        // If the function wasn't recompiled, don't populate its address.
        if (func_label != nullptr) {
            ret.functions[func_index] = reinterpret_cast<recomp_func_t*>(sljit_get_label_addr(func_label));
        }
    }
    context->func_labels.clear();

    // Get the reference symbol jump instruction addresses.
    ret.reference_symbol_jumps.resize(context->reference_symbol_jumps.size());
    for (size_t jump_index = 0; jump_index < context->reference_symbol_jumps.size(); jump_index++) {
        ReferenceJumpDetails& details = context->reference_symbol_jumps[jump_index].first;
        sljit_jump* jump = context->reference_symbol_jumps[jump_index].second;

        ret.reference_symbol_jumps[jump_index].first = details;
        ret.reference_symbol_jumps[jump_index].second = reinterpret_cast<void*>(jump->addr);
    }
    context->reference_symbol_jumps.clear();
    
    // Get the import jump instruction addresses.
    ret.import_jumps_by_index.reserve(context->import_jumps_by_index.size());
    for (auto& [jump_index, jump] : context->import_jumps_by_index) {
        ret.import_jumps_by_index.emplace(jump_index, reinterpret_cast<void*>(jump->addr));
    }
    context->import_jumps_by_index.clear();

    // Populate label addresses for the jump tables and place them in the output.
    for (auto& [labels, jump_table] : context->unlinked_jump_tables) {
        for (size_t entry_index = 0; entry_index < labels.size(); entry_index++) {
            sljit_label* cur_label = labels[entry_index];
            jump_table[entry_index] = reinterpret_cast<void*>(sljit_get_label_addr(cur_label));
        }
        ret.jump_tables.emplace_back(std::move(jump_table));
    }
    context->unlinked_jump_tables.clear();

    ret.executable_offset = sljit_get_executable_offset(compiler);

    sljit_free_compiler(compiler);
    compiler = nullptr;
    errored = false;

    return ret;
}

N64Recomp::LiveGeneratorOutput::~LiveGeneratorOutput() {
    if (code != nullptr) {
        sljit_free_code(code, nullptr);
        code = nullptr;
    }
}

size_t N64Recomp::LiveGeneratorOutput::num_reference_symbol_jumps() const {
    return reference_symbol_jumps.size();
}

void N64Recomp::LiveGeneratorOutput::set_reference_symbol_jump(size_t jump_index, recomp_func_t* func) {
    const auto& jump_entry = reference_symbol_jumps[jump_index];
    sljit_set_jump_addr(reinterpret_cast<sljit_uw>(jump_entry.second), reinterpret_cast<sljit_uw>(func), executable_offset);
}

N64Recomp::ReferenceJumpDetails N64Recomp::LiveGeneratorOutput::get_reference_symbol_jump_details(size_t jump_index) {
    return reference_symbol_jumps[jump_index].first;
}

void N64Recomp::LiveGeneratorOutput::populate_import_symbol_jumps(size_t import_index, recomp_func_t* func) {
    auto find_range = import_jumps_by_index.equal_range(import_index);
    for (auto it = find_range.first; it != find_range.second; ++it) {
        sljit_set_jump_addr(reinterpret_cast<sljit_uw>(it->second), reinterpret_cast<sljit_uw>(func), executable_offset);
    }
}

constexpr int get_gpr_context_offset(int gpr_index) {
    return offsetof(recomp_context, r0) + sizeof(recomp_context::r0) * gpr_index;
}

constexpr int get_fpr_single_context_offset(int fpr_index) {
    return offsetof(recomp_context, f0.fl) + sizeof(recomp_context::f0) * fpr_index;
}

constexpr int get_fpr_double_context_offset(int fpr_index) {
    return offsetof(recomp_context, f0.d) + sizeof(recomp_context::f0) * fpr_index;
}

constexpr int get_fpr_u32l_context_offset(int fpr_index) {
    if (fpr_index & 1) {
        // TODO implement odd floats.
        assert(false);
        return -1;
        // return fmt::format("ctx->f_odd[({} - 1) * 2]", fpr_index);
    }
    else {
        return offsetof(recomp_context, f0.u32l) + sizeof(recomp_context::f0) * fpr_index;
    }
}

constexpr int get_fpr_u64_context_offset(int fpr_index) {
    return offsetof(recomp_context, f0.u64) + sizeof(recomp_context::f0) * fpr_index;
}

void get_gpr_values(int gpr, sljit_sw& out, sljit_sw& outw) {
    if (gpr == 0) {
        out = SLJIT_IMM;
        outw = 0;
    }
    else {
        out = SLJIT_MEM1(Registers::ctx);
        outw = get_gpr_context_offset(gpr);
    }
}

bool get_operand_values(N64Recomp::Operand operand, const N64Recomp::InstructionContext& context, sljit_sw& out, sljit_sw& outw) {
    using namespace N64Recomp;

    switch (operand) {
        case Operand::Rd:
            get_gpr_values(context.rd, out, outw);
            break;
        case Operand::Rs:
            get_gpr_values(context.rs, out, outw);
            break;
        case Operand::Rt:
            get_gpr_values(context.rt, out, outw);
            break;
        case Operand::Fd:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_single_context_offset(context.fd);
            break;
        case Operand::Fs:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_single_context_offset(context.fs);
            break;
        case Operand::Ft:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_single_context_offset(context.ft);
            break;
        case Operand::FdDouble:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_double_context_offset(context.fd);
            break;
        case Operand::FsDouble:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_double_context_offset(context.fs);
            break;
        case Operand::FtDouble:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_double_context_offset(context.ft);
            break;
        case Operand::FdU32L:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u32l_context_offset(context.fd);
            break;
        case Operand::FsU32L:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u32l_context_offset(context.fs);
            break;
        case Operand::FtU32L:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u32l_context_offset(context.ft);
            break;
        case Operand::FdU32H:
            assert(false);
            return false;
        case Operand::FsU32H:
            assert(false);
            return false;
        case Operand::FtU32H:
            assert(false);
            return false;
        case Operand::FdU64:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u64_context_offset(context.fd);
            break;
        case Operand::FsU64:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u64_context_offset(context.fs);
            break;
        case Operand::FtU64:
            out = SLJIT_MEM1(Registers::ctx);
            outw = get_fpr_u64_context_offset(context.ft);
            break;
        case Operand::ImmU16:
            out = SLJIT_IMM;
            outw = (sljit_sw)(uint16_t)context.imm16;
            break;
        case Operand::ImmS16:
            out = SLJIT_IMM;
            outw = (sljit_sw)(int16_t)context.imm16;
            break;
        case Operand::Sa:
            out = SLJIT_IMM;
            outw = context.sa;
            break;
        case Operand::Sa32:
            out = SLJIT_IMM;
            outw = context.sa + 32;
            break;
        case Operand::Cop1cs:
            out = Registers::c1cs;
            outw = 0;
            break;
        case Operand::Hi:
            out = Registers::hi;
            outw = 0;
            break;
        case Operand::Lo:
            out = Registers::lo;
            outw = 0;
            break;
        case Operand::Zero:
            out = SLJIT_IMM;
            outw = 0;
            break;
    }
    return true;
}

bool outputs_to_zero(N64Recomp::Operand output, const N64Recomp::InstructionContext& ctx) {
    if (output == N64Recomp::Operand::Rd && ctx.rd == 0) {
        return true;
    }
    if (output == N64Recomp::Operand::Rt && ctx.rt == 0) {
        return true;
    }
    if (output == N64Recomp::Operand::Rs && ctx.rs == 0) {
        return true;
    }
    return false;
}

void N64Recomp::LiveGenerator::process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const {
    // Skip instructions that output to $zero
    if (outputs_to_zero(op.output, ctx)) {
        return;
    }
 
    sljit_sw dst;
    sljit_sw dstw;
    sljit_sw src1;
    sljit_sw src1w;
    sljit_sw src2;
    sljit_sw src2w;
    bool output_good = get_operand_values(op.output, ctx, dst, dstw);
    bool input0_good = get_operand_values(op.operands.operands[0], ctx, src1, src1w);
    bool input1_good = get_operand_values(op.operands.operands[1], ctx, src2, src2w);

    if (!output_good || !input0_good || !input1_good) {
        assert(false);
        errored = true;
        return;
    }

    // If a relocation is present, perform the relocation and change src1/src1w to use the relocated value.
    if (ctx.reloc_type != RelocType::R_MIPS_NONE) {
        // Only allow LO16 relocations.
        if (ctx.reloc_type != RelocType::R_MIPS_LO16) {
            assert(false);
            errored = true;
            return;
        }
        // Only allow relocations on immediates.
        if (src2 != SLJIT_IMM) {
            assert(false);
            errored = true;
            return;
        }
        // Only allow relocations on loads and adds.
        switch (op.type) {
            case BinaryOpType::LD:
            case BinaryOpType::LW:
            case BinaryOpType::LWU:
            case BinaryOpType::LH:
            case BinaryOpType::LHU:
            case BinaryOpType::LB:
            case BinaryOpType::LBU:
            case BinaryOpType::LDL:
            case BinaryOpType::LDR:
            case BinaryOpType::LWL:
            case BinaryOpType::LWR:
            case BinaryOpType::Add64:
            case BinaryOpType::Add32:
                break;
            default:
                // Relocations aren't allowed on this instruction.
                assert(false);
                errored = true;
                return;
        }
        // Load the relocated address into temp2.
        load_relocated_address(ctx, Registers::arithmetic_temp1);
        // Extract the LO16 value from the full address (sign extended lower 16 bits).
        sljit_emit_op1(compiler, SLJIT_MOV_S16, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
        // Replace the immediate input (src2) with the LO16 value.
        src2 = Registers::arithmetic_temp1;
        src2w = 0;
    }

    // TODO validate that the unary ops are valid for the current binary op.
    if (op.operands.operand_operations[0] != UnaryOpType::None &&
        op.operands.operand_operations[0] != UnaryOpType::ToU64 &&
        op.operands.operand_operations[0] != UnaryOpType::ToS64 &&
        op.operands.operand_operations[0] != UnaryOpType::ToU32)
    {
        assert(false);
        errored = true;
        return;
    }
    
    if (op.operands.operand_operations[1] != UnaryOpType::None &&
        op.operands.operand_operations[1] != UnaryOpType::ToU64 &&
        op.operands.operand_operations[1] != UnaryOpType::ToS64 &&
        op.operands.operand_operations[1] != UnaryOpType::Mask5 && // Only for 32-bit shifts
        op.operands.operand_operations[1] != UnaryOpType::Mask6) // Only for 64-bit shifts
    {
        assert(false);
        errored = true;
        return;
    }

    bool cmp_unsigned = op.operands.operand_operations[0] != UnaryOpType::ToS64;

    auto sign_extend_and_store = [dst, dstw, this]() {
        // Sign extend the result.
        sljit_emit_op1(this->compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
        // Store the result back into the context.
        sljit_emit_op1(this->compiler, SLJIT_MOV_P, dst, dstw, Registers::arithmetic_temp1, 0);
    };

    auto do_op32 = [src1, src1w, src2, src2w, this, &sign_extend_and_store](sljit_s32 op) {
        sljit_emit_op2(this->compiler, op, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);
        sign_extend_and_store();
    };

    auto do_op64 = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op) {
        sljit_emit_op2(this->compiler, op, dst, dstw, src1, src1w, src2, src2w);
    };

    auto do_float_op = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op) {
        sljit_emit_fop2(this->compiler, op, dst, dstw, src1, src1w, src2, src2w);
    };

    auto do_load_op = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op, int address_xor) {
        // TODO 0 immediate optimization.

        // Add the base and immediate into the arithemtic temp.
        sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);

        if (address_xor != 0) {
            // xor the address with the specified amount
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, address_xor);
        }
        
        // Load the value at rdram + address into the arithemtic temp with the given operation to allow for sign-extension or zero-extension.
        sljit_emit_op1(compiler, op, Registers::arithmetic_temp1, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0);

        // Move the arithmetic temp into the destination.
        sljit_emit_op1(compiler, SLJIT_MOV, dst, dstw, Registers::arithmetic_temp1, 0);
    };

    auto do_compare_op = [cmp_unsigned, dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op_unsigned, sljit_s32 op_signed) {
        // Pick the operation based on the signedness of the comparison.
        sljit_s32 op = cmp_unsigned ? op_unsigned : op_signed;

        // Pick the flags to set based on the operation.
        sljit_s32 flags;
        if (op <= SLJIT_NOT_ZERO) {
            flags = SLJIT_SET_Z;
        } else
        {
            flags = SLJIT_SET(op);
        }

        // Perform a subtraction with the determined flag.
        sljit_emit_op2u(compiler, SLJIT_SUB | flags, src1, src1w, src2, src2w);
        
        // Move the operation's flag into the destination.
        sljit_emit_op_flags(compiler, SLJIT_MOV, dst, dstw, op);
    };

    auto do_float_compare_op = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 flag_op, sljit_s32 set_op, bool double_precision) {
        // Pick the operation based on the signedness of the comparison.
        sljit_s32 compare_op = set_op | (double_precision ? SLJIT_CMP_F64 : SLJIT_CMP_F32);

        // Perform the comparison with the determined operation.
        // Float comparisons use fop1 and put the left hand side in dst.
        sljit_emit_fop1(compiler, compare_op, src1, src1w, src2, src2w);
        
        // Move the operation's flag into the destination.
        sljit_emit_op_flags(compiler, SLJIT_MOV, dst, dstw, flag_op);
    };

    auto do_unaligned_load_op = [dst, dstw, src1, src1w, src2, src2w, this](bool left, bool doubleword) {
        // TODO 0 immediate optimization.

        // Determine the shift direction to use for calculating the mask and shifting the loaded value.
        sljit_sw shift_op = left ? SLJIT_SHL : SLJIT_LSHR;
        // Determine the operation's word size.
        sljit_sw word_size = doubleword ? 8 : 4;

        // Add the base and immediate into the temp1.
        // addr = base + offset
        sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);

        // Mask the address with the alignment mask to get the misalignment and put it in temp2.
        // misalignment = addr & (word_size - 1);
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, word_size - 1);

        // Mask the address with ~alignment_mask to get the aligned address and put it in temp1.
        // addr = addr & ~(word_size - 1);
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, ~(word_size - 1));

        // Load the word at rdram + aligned address into the temp1 with sign-extension.
        // loaded_value = *addr
        if (doubleword) {
            // Rotate the loaded doubleword by 32 bits to swap the two words into the right order.
            sljit_emit_op2(compiler, SLJIT_ROTL, Registers::arithmetic_temp1, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, SLJIT_IMM, 32);
        }
        else {
            // Use MOV_S32 to sign-extend the loaded word.
            sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0);
        }

        // Inverse the misalignment if this is a right load.
        if (!left) {
            // misalignment = (word_size - 1 - misalignment) * 8
            sljit_emit_op2(compiler, SLJIT_SUB, Registers::arithmetic_temp2, 0, SLJIT_IMM, word_size - 1, Registers::arithmetic_temp2, 0);
        }

        // Calculate the misalignment shift and put it into temp2.
        // misalignment_shift = misalignment * 8
        sljit_emit_op2(compiler, SLJIT_SHL, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp2, 0, SLJIT_IMM, 3);

        // Calculate the misalignment mask and put it into temp3. Use a 32-bit shift if this is a 32-bit operation.
        // misalignment_mask = word(-1) SHIFT misalignment_shift
        sljit_emit_op2(compiler, doubleword ? shift_op : (shift_op | SLJIT_32),
            Registers::arithmetic_temp3, 0,
            SLJIT_IMM, doubleword ? uint64_t(-1) : uint32_t(-1),
            Registers::arithmetic_temp2, 0);

        if (!doubleword) {
            // Sign extend the misalignment mask.
            // misalignment_mask = ((uint64_t)(int32_t)misalignment_mask)
            sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp3, 0);
        }

        // Shift the loaded value by the misalignment shift and put it into temp1.
        // loaded_value SHIFT misalignment_shift
        sljit_emit_op2(compiler, shift_op, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp2, 0);

        if (left && !doubleword) {
            // Sign extend the loaded value.
            // loaded_value = (uint64_t)(int32_t)loaded_value
            sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
        }

        // Mask the shifted loaded value by the misalignment mask.
        // loaded_value &= misalignment_mask
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp3, 0);

        // Invert the misalignment mask and store it into temp3.
        // misalignment_mask = ~misalignment_mask
        sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp3, 0, SLJIT_IMM, sljit_sw(-1));

        // Mask the initial value (stored in the destination) with the misalignment mask and place it into temp3.
        // masked_value = initial_value & misalignment_mask
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp3, 0, dst, dstw, Registers::arithmetic_temp3, 0);

        // Combine the masked initial value with the shifted loaded value and store it in the destination.
        // out = masked_value | loaded_value
        sljit_emit_op2(compiler, SLJIT_OR, dst, dstw, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp1, 0);
    };

    switch (op.type) {
        // Addition/subtraction
        case BinaryOpType::Add32:
            do_op32(SLJIT_ADD32);
            break;
        case BinaryOpType::Sub32:
            do_op32(SLJIT_SUB32);
            break;
        case BinaryOpType::Add64:
            do_op64(SLJIT_ADD);
            break;
        case BinaryOpType::Sub64:
            do_op64(SLJIT_SUB);
            break;

        // Float arithmetic
        case BinaryOpType::AddFloat:
            do_float_op(SLJIT_ADD_F32);
            break;
        case BinaryOpType::AddDouble:
            do_float_op(SLJIT_ADD_F64);
            break;
        case BinaryOpType::SubFloat:
            do_float_op(SLJIT_SUB_F32);
            break;
        case BinaryOpType::SubDouble:
            do_float_op(SLJIT_SUB_F64);
            break;
        case BinaryOpType::MulFloat:
            do_float_op(SLJIT_MUL_F32);
            break;
        case BinaryOpType::MulDouble:
            do_float_op(SLJIT_MUL_F64);
            break;
        case BinaryOpType::DivFloat:
            do_float_op(SLJIT_DIV_F32);
            break;
        case BinaryOpType::DivDouble:
            do_float_op(SLJIT_DIV_F64);
            break;

        // Bitwise
        case BinaryOpType::And64:
            do_op64(SLJIT_AND);
            break;
        case BinaryOpType::Or64:
            do_op64(SLJIT_OR);
            break;
        case BinaryOpType::Nor64:
            // Bitwise or the two registers and move the result into the temp, then invert the result and move it into the destination.
            sljit_emit_op2(this->compiler, SLJIT_OR, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);
            sljit_emit_op2(this->compiler, SLJIT_XOR, dst, dstw, Registers::arithmetic_temp1, 0, SLJIT_IMM, sljit_sw(-1));
            break;
        case BinaryOpType::Xor64:
            do_op64(SLJIT_XOR);
            break;
        case BinaryOpType::Sll32:
            // TODO only mask if the second input's op is Mask5.
            do_op32(SLJIT_MSHL32);
            break;
        case BinaryOpType::Sll64:
            // TODO only mask if the second input's op is Mask6.
            do_op64(SLJIT_MSHL);
            break;
        case BinaryOpType::Srl32:
            // TODO only mask if the second input's op is Mask5.
            do_op32(SLJIT_MLSHR32);
            break;
        case BinaryOpType::Srl64:
            // TODO only mask if the second input's op is Mask6.
            do_op64(SLJIT_MLSHR);
            break;
        case BinaryOpType::Sra32:
            // Hardware bug: The input is not masked to 32 bits before right shifting, so bits from the upper half of the register will bleed into the lower half.
            // This means we have to use a 64-bit shift and manually mask the input before shifting.
            // TODO only mask if the second input's op is Mask5.
            sljit_emit_op2(this->compiler, SLJIT_AND32, Registers::arithmetic_temp1, 0, src2, src2w, SLJIT_IMM, 0b11111);
            sljit_emit_op2(this->compiler, SLJIT_MASHR, Registers::arithmetic_temp1, 0, src1, src1w, Registers::arithmetic_temp1, 0);
            sign_extend_and_store();
            break;
        case BinaryOpType::Sra64:
            // TODO only mask if the second input's op is Mask6.
            do_op64(SLJIT_MASHR);
            break;

        // Comparisons
        case BinaryOpType::Equal:
            do_compare_op(SLJIT_EQUAL, SLJIT_EQUAL);
            break;
        case BinaryOpType::NotEqual:
            do_compare_op(SLJIT_NOT_EQUAL, SLJIT_NOT_EQUAL);
            break;
        case BinaryOpType::Less:
            do_compare_op(SLJIT_LESS, SLJIT_SIG_LESS);
            break;
        case BinaryOpType::LessEq:
            do_compare_op(SLJIT_LESS_EQUAL, SLJIT_SIG_LESS_EQUAL);
            break;
        case BinaryOpType::Greater:
            do_compare_op(SLJIT_GREATER, SLJIT_SIG_GREATER);
            break;
        case BinaryOpType::GreaterEq:
            do_compare_op(SLJIT_GREATER_EQUAL, SLJIT_SIG_GREATER_EQUAL);
            break;
        case BinaryOpType::EqualFloat:
            do_float_compare_op(SLJIT_F_EQUAL, SLJIT_SET_F_EQUAL, false);
            break;
        case BinaryOpType::LessFloat:
            do_float_compare_op(SLJIT_F_LESS, SLJIT_SET_F_LESS, false);
            break;
        case BinaryOpType::LessEqFloat:
            do_float_compare_op(SLJIT_F_LESS_EQUAL, SLJIT_SET_F_LESS_EQUAL, false);
            break;
        case BinaryOpType::EqualDouble:
            do_float_compare_op(SLJIT_F_EQUAL, SLJIT_SET_F_EQUAL, true);
            break;
        case BinaryOpType::LessDouble:
            do_float_compare_op(SLJIT_F_LESS, SLJIT_SET_F_LESS, true);
            break;
        case BinaryOpType::LessEqDouble:
            do_float_compare_op(SLJIT_F_LESS_EQUAL, SLJIT_SET_F_LESS_EQUAL, true);
            break;

        // Loads
        case BinaryOpType::LD:
            // Add the base and immediate into the arithemtic temp.
            sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);
        
            // Load the value at rdram + address into the arithemtic temp and rotate it by 32 bits to swap the two words into the right order.
            sljit_emit_op2(compiler, SLJIT_ROTL, Registers::arithmetic_temp1, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, SLJIT_IMM, 32);

            // Move the arithmetic temp into the destination.
            sljit_emit_op1(compiler, SLJIT_MOV, dst, dstw, Registers::arithmetic_temp1, 0);
            break;
        case BinaryOpType::LW:
            do_load_op(SLJIT_MOV_S32, 0);
            break;
        case BinaryOpType::LWU:
            do_load_op(SLJIT_MOV_U32, 0);
            break;
        case BinaryOpType::LH:
            do_load_op(SLJIT_MOV_S16, 2);
            break;
        case BinaryOpType::LHU:
            do_load_op(SLJIT_MOV_U16, 2);
            break;
        case BinaryOpType::LB:
            do_load_op(SLJIT_MOV_S8, 3);
            break;
        case BinaryOpType::LBU:
            do_load_op(SLJIT_MOV_U8, 3);
            break;
        case BinaryOpType::LDL:
            do_unaligned_load_op(true, true);
            break;
        case BinaryOpType::LDR:
            do_unaligned_load_op(false, true);
            break;
        case BinaryOpType::LWL:
            do_unaligned_load_op(true, false);
            break;
        case BinaryOpType::LWR:
            do_unaligned_load_op(false, false);
            break;
        default:
            assert(false);
            errored = true;
            return;
    }
}

// TODO these four operations should use banker's rounding, but roundeven is C23 so it's unavailable here.
int32_t do_round_w_s(float num) {
    return lroundf(num);
}

int32_t do_round_w_d(double num) {
    return lround(num);
}

int64_t do_round_l_s(float num) {
    return llroundf(num);
}

int64_t do_round_l_d(double num) {
    return llround(num);
}

int32_t do_ceil_w_s(float num) {
    return (int32_t)ceilf(num);
}

int32_t do_ceil_w_d(double num) {
    return (int32_t)ceil(num);
}

int64_t do_ceil_l_s(float num) {
    return (int64_t)ceilf(num);
}

int64_t do_ceil_l_d(double num) {
    return (int64_t)ceil(num);
}

int32_t do_floor_w_s(float num) {
    return (int32_t)floorf(num);
}

int32_t do_floor_w_d(double num) {
    return (int32_t)floor(num);
}

int64_t do_floor_l_s(float num) {
    return (int64_t)floorf(num);
}

int64_t do_floor_l_d(double num) {
    return (int64_t)floor(num);
}

void N64Recomp::LiveGenerator::load_relocated_address(const InstructionContext& ctx, int reg) const {
    // Get the pointer to the section address.
    int32_t* section_addr_ptr = (ctx.reloc_tag_as_reference ? inputs.reference_section_addresses : inputs.local_section_addresses) + ctx.reloc_section_index;

    // Load the section's address into the target register.
    sljit_emit_op1(compiler, SLJIT_MOV_S32, reg, 0, SLJIT_MEM0(), sljit_sw(section_addr_ptr));

    // Don't emit the add if the offset is zero (small optimization).
    if (ctx.reloc_target_section_offset != 0) {
        // Add the reloc section offset to the section's address and put the result in R0.
        sljit_emit_op2(compiler, SLJIT_ADD, reg, 0, reg, 0, SLJIT_IMM, ctx.reloc_target_section_offset);
    }
}

void N64Recomp::LiveGenerator::process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const {
    // Skip instructions that output to $zero
    if (outputs_to_zero(op.output, ctx)) {
        return;
    }

    sljit_sw dst;
    sljit_sw dstw;
    sljit_sw src;
    sljit_sw srcw;
    bool output_good = get_operand_values(op.output, ctx, dst, dstw);
    bool input_good = get_operand_values(op.input, ctx, src, srcw);

    if (!output_good || !input_good) {
        assert(false);
        errored = true;
        return;
    }

    // If a relocation is needed for the input operand, perform the relocation and store the result directly.
    if (ctx.reloc_type != RelocType::R_MIPS_NONE) {
        // Only allow relocation of lui with an immediate.
        if (op.operation != UnaryOpType::Lui || op.input != Operand::ImmU16) {
            assert(false);
            errored = true;
            return;
        }
        // Only allow HI16 relocs.
        if (ctx.reloc_type != RelocType::R_MIPS_HI16) {
            assert(false);
            errored = true;
            return;
        }
        // Load the relocated address into temp1.
        load_relocated_address(ctx, Registers::arithmetic_temp1);

        // HI16 reloc on a lui
        // The 32-bit address (a) is equal to section address + section offset
        // The 16-bit immediate is equal to (a - (int16_t)a) >> 16
        // Therefore, the register should be set to (int32_t)(a - (int16_t)a) as the shifts cancel out and the lower 16 bits are zero.

        // Extract a sign extended 16-bit value from the lower half of the relocated address and put it in temp2.
        sljit_emit_op1(compiler, SLJIT_MOV_S16, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp1, 0);

        // Subtract the sign extended 16-bit value from the full address to get the HI16 value and place it in the destination.
        sljit_emit_op2(compiler, SLJIT_SUB, dst, dstw, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp2, 0);
        return;
    }

    sljit_s32 jit_op = SLJIT_BREAKPOINT;

    bool float_op = false;
    bool func_float_op = false;

    auto emit_s_func = [this, src, srcw, dst, dstw, &func_float_op](float (*func)(float)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F32, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(F32, F32), SLJIT_IMM, sljit_sw(func));
        sljit_emit_fop1(compiler, SLJIT_MOV_F32, dst, dstw, SLJIT_RETURN_FREG, 0);
    };

    auto emit_d_func = [this, src, srcw, dst, dstw, &func_float_op](double (*func)(double)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(F64, F64), SLJIT_IMM, sljit_sw(func));
        sljit_emit_fop1(compiler, SLJIT_MOV_F64, dst, dstw, SLJIT_RETURN_FREG, 0);
    };

    auto emit_l_from_s_func = [this, src, srcw, dst, dstw, &func_float_op](int64_t (*func)(float)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F32, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, F32), SLJIT_IMM, sljit_sw(func));
        sljit_emit_op1(compiler, SLJIT_MOV, dst, dstw, SLJIT_RETURN_REG, 0);
    };

    auto emit_w_from_s_func = [this, src, srcw, dst, dstw, &func_float_op](int32_t (*func)(float)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F32, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(32, F32), SLJIT_IMM, sljit_sw(func));
        sljit_emit_op1(compiler, SLJIT_MOV_S32, dst, dstw, SLJIT_RETURN_REG, 0);
    };

    auto emit_l_from_d_func = [this, src, srcw, dst, dstw, &func_float_op](int64_t (*func)(double)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, F64), SLJIT_IMM, sljit_sw(func));
        sljit_emit_op1(compiler, SLJIT_MOV, dst, dstw, SLJIT_RETURN_REG, 0);
    };

    auto emit_w_from_d_func = [this, src, srcw, dst, dstw, &func_float_op](int32_t (*func)(double)) {
        func_float_op = true;

        sljit_emit_fop1(compiler, SLJIT_MOV_F64, SLJIT_FR0, 0, src, srcw);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(32, F64), SLJIT_IMM, sljit_sw(func));
        sljit_emit_op1(compiler, SLJIT_MOV_S32, dst, dstw, SLJIT_RETURN_REG, 0);
    };

    switch (op.operation) {
        case UnaryOpType::Lui:
            if (src != SLJIT_IMM) {
                assert(false);
                errored = true;
                break;
            }
            src = SLJIT_IMM;
            srcw = (sljit_sw)(int32_t)(srcw << 16);
            jit_op = SLJIT_MOV;
            break;
        case UnaryOpType::NegateFloat:
            jit_op = SLJIT_NEG_F32;
            float_op = true;
            break;
        case UnaryOpType::NegateDouble:
            jit_op = SLJIT_NEG_F64;
            float_op = true;
            break;
        case UnaryOpType::AbsFloat:
            jit_op = SLJIT_ABS_F32;
            float_op = true;
            break;
        case UnaryOpType::AbsDouble:
            jit_op = SLJIT_ABS_F64;
            float_op = true;
            break;
        case UnaryOpType::SqrtFloat:
            emit_s_func(sqrtf);
            break;
        case UnaryOpType::SqrtDouble:
            emit_d_func(sqrt);
            break;
        case UnaryOpType::ConvertSFromW:
            jit_op = SLJIT_CONV_F32_FROM_S32;
            float_op = true;
            break;
        case UnaryOpType::ConvertWFromS:
            emit_w_from_s_func(do_cvt_w_s);
            break;
        case UnaryOpType::ConvertDFromW:
            jit_op = SLJIT_CONV_F64_FROM_S32;
            float_op = true;
            break;
        case UnaryOpType::ConvertWFromD:
            emit_w_from_d_func(do_cvt_w_d);
            break;
        case UnaryOpType::ConvertDFromS:
            jit_op = SLJIT_CONV_F64_FROM_F32;
            float_op = true;
            break;
        case UnaryOpType::ConvertSFromD:
            // SLJIT_CONV_F32_FROM_F64 uses the current rounding mode, just as CVT_S_D does.
            jit_op = SLJIT_CONV_F32_FROM_F64;
            float_op = true;
            break;
        case UnaryOpType::ConvertDFromL:
            jit_op = SLJIT_CONV_F64_FROM_SW;
            float_op = true;
            break;
        case UnaryOpType::ConvertLFromD:
            emit_l_from_d_func(do_cvt_l_d);
            break;
        case UnaryOpType::ConvertSFromL:
            jit_op = SLJIT_CONV_F32_FROM_SW;
            float_op = true;
            break;
        case UnaryOpType::ConvertLFromS:
            emit_l_from_s_func(do_cvt_l_s);
            break;
        case UnaryOpType::TruncateWFromS:
            // SLJIT_CONV_S32_FROM_F32 rounds towards zero, just as TRUNC_W_S does.
            jit_op = SLJIT_CONV_S32_FROM_F32;
            float_op = true;
            break;
        case UnaryOpType::TruncateWFromD:
            // SLJIT_CONV_S32_FROM_F64 rounds towards zero, just as TRUNC_W_D does.
            jit_op = SLJIT_CONV_S32_FROM_F64;
            float_op = true;
            break;
        case UnaryOpType::TruncateLFromS:
            // SLJIT_CONV_SW_FROM_F32 rounds towards zero, just as TRUNC_L_S does.
            jit_op = SLJIT_CONV_SW_FROM_F32;
            float_op = true;
            break;
        case UnaryOpType::TruncateLFromD:
            // SLJIT_CONV_SW_FROM_F64 rounds towards zero, just as TRUNC_L_D does.
            jit_op = SLJIT_CONV_SW_FROM_F64;
            float_op = true;
            break;
        case UnaryOpType::RoundWFromS:
            emit_w_from_s_func(do_round_w_s);
            break;
        case UnaryOpType::RoundWFromD:
            emit_w_from_d_func(do_round_w_d);
            break;
        case UnaryOpType::RoundLFromS:
            emit_l_from_s_func(do_round_l_s);
            break;
        case UnaryOpType::RoundLFromD:
            emit_l_from_d_func(do_round_l_d);
            break;
        case UnaryOpType::CeilWFromS:
            emit_w_from_s_func(do_ceil_w_s);
            break;
        case UnaryOpType::CeilWFromD:
            emit_w_from_d_func(do_ceil_w_d);
            break;
        case UnaryOpType::CeilLFromS:
            emit_l_from_s_func(do_ceil_l_s);
            break;
        case UnaryOpType::CeilLFromD:
            emit_l_from_d_func(do_ceil_l_d);
            break;
        case UnaryOpType::FloorWFromS:
            emit_w_from_s_func(do_floor_w_s);
            break;
        case UnaryOpType::FloorWFromD:
            emit_w_from_d_func(do_floor_w_d);
            break;
        case UnaryOpType::FloorLFromS:
            emit_l_from_s_func(do_floor_l_s);
            break;
        case UnaryOpType::FloorLFromD:
            emit_l_from_d_func(do_floor_l_d);
            break;
        case UnaryOpType::None:
            jit_op = SLJIT_MOV;
            break;
        case UnaryOpType::ToS32:
        case UnaryOpType::ToInt32:
            // sljit won't emit a sign extension with SLJIT_MOV_32 if the destination is memory,
            // so emit an explicit move into a register and set that register as the new src.
            sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, src, srcw);
            // Replace the original input with the temporary register.
            src = Registers::arithmetic_temp1;
            srcw = 0;
            jit_op = SLJIT_MOV;
            break;
        // Unary ops that can't be used as a standalone operation
        case UnaryOpType::ToU32:
        case UnaryOpType::ToS64:
        case UnaryOpType::ToU64:
        case UnaryOpType::Mask5:
        case UnaryOpType::Mask6:
            assert(false && "Unsupported unary op");
            errored = true;
            return;
    }

    if (func_float_op) {
        // Already handled by the lambda.
    }
    else if (float_op) {
        sljit_emit_fop1(compiler, jit_op, dst, dstw, src, srcw);
    }
    else {
        sljit_emit_op1(compiler, jit_op, dst, dstw, src, srcw);
    }
}

void N64Recomp::LiveGenerator::process_store_op(const StoreOp& op, const InstructionContext& ctx) const {
    sljit_sw src;
    sljit_sw srcw;
    sljit_sw imm = (sljit_sw)(int16_t)ctx.imm16;

    get_operand_values(op.value_input, ctx, src, srcw);

    // Only LO16 relocs are valid on stores.
    if (ctx.reloc_type != RelocType::R_MIPS_NONE && ctx.reloc_type != RelocType::R_MIPS_LO16) {
        assert(false);
        errored = true;
        return;
    }

    if (ctx.reloc_type == RelocType::R_MIPS_LO16) {
        // Load the relocated address into temp1.
        load_relocated_address(ctx, Registers::arithmetic_temp1);
        // Extract the LO16 value from the full address (sign extended lower 16 bits).
        sljit_emit_op1(compiler, SLJIT_MOV_S16, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
        // Add the base register (rs) to the LO16 immediate.
        sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(ctx.rs));
    }
    else {
        // TODO 0 immediate optimization.

        // Add the base register (rs) and the immediate to get the address and store it in the arithemtic temp.
        sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(ctx.rs), SLJIT_IMM, imm);
    }

    auto do_unaligned_store_op = [src, srcw, this](bool left, bool doubleword) {
        // Determine the shift direction to use for calculating the mask and shifting the loaded value.
        sljit_sw shift_op = left ? SLJIT_LSHR : SLJIT_SHL;
        // Determine the operation's word size.
        sljit_sw word_size = doubleword ? 8 : 4;

        // Mask the address with the alignment mask to get the misalignment and put it in temp2.
        // misalignment = addr & (word_size - 1);
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, word_size - 1);

        // Mask the address with ~alignment_mask to get the aligned address and put it in temp1.
        // addr = addr & ~(word_size - 1);
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, ~(word_size - 1));

        // Load the word at rdram + aligned address into the temp1 with sign-extension.
        // loaded_value = *addr
        if (doubleword) {
            // Rotate the loaded doubleword by 32 bits to swap the two words into the right order.
            sljit_emit_op2(compiler, SLJIT_ROTL, Registers::arithmetic_temp3, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, SLJIT_IMM, 32);
        }
        else {
            // Use MOV_S32 to sign-extend the loaded word.
            sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp3, 0, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0);
        }

        // Inverse the misalignment if this is a right load.
        if (!left) {
            // misalignment = (word_size - 1 - misalignment) * 8
            sljit_emit_op2(compiler, SLJIT_SUB, Registers::arithmetic_temp2, 0, SLJIT_IMM, word_size - 1, Registers::arithmetic_temp2, 0);
        }

        // Calculate the misalignment shift and put it into temp2.
        // misalignment_shift = misalignment * 8
        sljit_emit_op2(compiler, SLJIT_SHL, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp2, 0, SLJIT_IMM, 3);

        // Shift the input value by the misalignment shift and put it into temp4.
        // input_value SHIFT= misalignment_shift
        sljit_emit_op2(compiler, shift_op, Registers::arithmetic_temp4, 0, src, srcw, Registers::arithmetic_temp2, 0);

        // Calculate the misalignment mask and put it into temp2. Use a 32-bit shift if this is a 32-bit operation.
        // misalignment_mask = word(-1) SHIFT misalignment_shift
        sljit_emit_op2(compiler, doubleword ? shift_op : (shift_op | SLJIT_32),
            Registers::arithmetic_temp2, 0,
            SLJIT_IMM, doubleword ? uint64_t(-1) : uint32_t(-1),
            Registers::arithmetic_temp2, 0);

        // Mask the input value with the misalignment mask and place it into temp4.
        // masked_value = shifted_value & misalignment_mask
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp2, 0);

        // Invert the misalignment mask and store it into temp2.
        // misalignment_mask = ~misalignment_mask
        sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp2, 0, Registers::arithmetic_temp2, 0, SLJIT_IMM, sljit_sw(-1));

        // Mask the loaded value by the misalignment mask.
        // input_value &= misalignment_mask
        sljit_emit_op2(compiler, SLJIT_AND, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp2, 0);

        // Combine the masked initial value with the shifted loaded value and store it in the destination.
        // out = masked_value | input_value
        if (doubleword) {
            // Combine the values into a temp so that it can be rotated to the correct word order.
            sljit_emit_op2(compiler, SLJIT_OR, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp3, 0);
            sljit_emit_op2(compiler, SLJIT_ROTL, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, Registers::arithmetic_temp4, 0, SLJIT_IMM, 32);
        }
        else {
            sljit_emit_op2(compiler, SLJIT_OR32, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp3, 0);
        }
    };

    switch (op.type) {
        case StoreOpType::SD:
        case StoreOpType::SDC1:        
            // Rotate the arithmetic temp by 32 bits to swap the words and move it into the destination.
            sljit_emit_op2(compiler, SLJIT_ROTL, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw, SLJIT_IMM, 32);
            break;
        case StoreOpType::SDL:
            do_unaligned_store_op(true, true);
            break;
        case StoreOpType::SDR:
            do_unaligned_store_op(false, true);
            break;
        case StoreOpType::SW:
        case StoreOpType::SWC1:
            // store the 32-bit value at address + rdram
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw);
            break;
        case StoreOpType::SWL:
            do_unaligned_store_op(true, false);
            break;
        case StoreOpType::SWR:
            do_unaligned_store_op(false, false);
            break;
        case StoreOpType::SH:
            // xor the address with 2
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, 2);
            // store the 16-bit value at address + rdram
            sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw);
            break;
        case StoreOpType::SB:
            // xor the address with 3
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, 3);
            // store the 8-bit value at address + rdram
            sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw);
            break;
    }
}

void N64Recomp::LiveGenerator::emit_function_start(const std::string& function_name, size_t func_index) const {
    context->function_name = function_name;
    context->func_labels[func_index] = sljit_emit_label(compiler);
    // sljit_emit_op0(compiler, SLJIT_BREAKPOINT);
    sljit_emit_enter(compiler, 0, SLJIT_ARGS2V(P, P), 4 | SLJIT_ENTER_FLOAT(1), 5 | SLJIT_ENTER_FLOAT(0), 0);
    sljit_emit_op2(compiler, SLJIT_SUB, Registers::rdram, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    
    // Check if this function's entry is hooked and emit the hook call if so.
    auto find_hook_it = inputs.entry_func_hooks.find(func_index);
    if (find_hook_it != inputs.entry_func_hooks.end()) {
        // Load rdram and ctx into R0 and R1.
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
        // Load the hook's index into R2.
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, find_hook_it->second);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, P, W), SLJIT_IMM, sljit_sw(inputs.run_hook));
    }
}

void N64Recomp::LiveGenerator::emit_function_end() const {
    // Check that all jumps have been paired to a label.
    if (!context->pending_jumps.empty()) {
        assert(false);
        errored = true;
    }
    
    // Populate the labels for pending switches and move them into the unlinked jump tables.
    bool invalid_switch = false;
    for (size_t switch_index = 0; switch_index < context->switch_jump_labels.size(); switch_index++) {
        const std::vector<std::string>& cur_labels = context->switch_jump_labels[switch_index];
        std::vector<sljit_label*> cur_label_addrs{};
        cur_label_addrs.resize(cur_labels.size());
        for (size_t case_index = 0; case_index < cur_labels.size(); case_index++) {
            // Find the label.
            auto find_it = context->labels.find(cur_labels[case_index]);
            if (find_it == context->labels.end()) {
                // Label not found, invalid switch.
                // Track this in a variable instead of returning immediately so that the pending labels are still cleared.
                invalid_switch = true;
                break;
            }
            cur_label_addrs[case_index] = find_it->second;
        }
        context->unlinked_jump_tables.emplace_back(
            std::make_pair<std::vector<sljit_label*>, std::unique_ptr<void*[]>>(
                std::move(cur_label_addrs),
                std::move(context->pending_jump_tables[switch_index])
            )
        );
    }
    context->switch_jump_labels.clear();
    context->pending_jump_tables.clear();

    // Clear the labels to prevent labels from one function being jumped to by another.
    context->labels.clear();

    if (invalid_switch) {
        assert(false);
        errored = true;
    }
}

void N64Recomp::LiveGenerator::emit_function_call_lookup(uint32_t addr) const {
    // Load the address immediate into the first argument. 
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_IMM, int32_t(addr));
    
    // Call get_function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, 32), SLJIT_IMM, sljit_sw(inputs.get_function));
    
    // Copy the return value into R3 so that it can be used for icall
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_RETURN_REG, 0);
    
    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);

    // Call the function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P), SLJIT_R3, 0);
}

void N64Recomp::LiveGenerator::emit_function_call_by_register(int reg) const {
    // Load the register's value into the first argument. 
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(reg));

    // Call get_function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, 32), SLJIT_IMM, sljit_sw(inputs.get_function));

    // Copy the return value into R3 so that it can be used for icall
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_RETURN_REG, 0);

    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);

    // Call the function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P), SLJIT_R3, 0);
}

void N64Recomp::LiveGenerator::emit_function_call_reference_symbol(const Context&, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const {
    (void)symbol_index;

    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
    // sljit_emit_op0(compiler, SLJIT_BREAKPOINT);
    // Call the function and save the jump to set its label later on.
    sljit_jump* call_jump = sljit_emit_call(compiler, SLJIT_CALL | SLJIT_REWRITABLE_JUMP, SLJIT_ARGS2V(P, P));
    // Set a dummy jump value, this will get replaced during reference/import symbol jump population.
    if (section_index == N64Recomp::SectionImport) {
        sljit_set_target(call_jump, sljit_uw(-1));
        context->import_jumps_by_index.emplace(symbol_index, call_jump);
    }
    else {
        sljit_set_target(call_jump, sljit_uw(-2));
        context->reference_symbol_jumps.emplace_back(std::make_pair(
            ReferenceJumpDetails{
                .section = section_index,
                .section_offset = target_section_offset
            },
            call_jump
        ));
    }
}

void N64Recomp::LiveGenerator::emit_function_call(const Context&, size_t function_index) const {
    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
    // Call the function and save the jump to set its label later on.
    sljit_jump* call_jump = sljit_emit_call(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P));
    context->inner_calls.emplace_back(InnerCall{ .target_func_index = function_index, .jump = call_jump });
}

void N64Recomp::LiveGenerator::emit_named_function_call(const std::string& function_name) const {
    // The live recompiler can't call functions by name. This is only used for statics, so it's not an issue.
    assert(false);
    errored = true;
}

void N64Recomp::LiveGenerator::emit_goto(const std::string& target) const {
    sljit_jump* jump = sljit_emit_jump(compiler, SLJIT_JUMP);
    // Check if the label already exists.
    auto find_it = context->labels.find(target);
    if (find_it != context->labels.end()) {
        sljit_set_label(jump, find_it->second);
    }
    // It doesn't, so queue this as a pending jump to be resolved later.
    else {
        context->pending_jumps[target].push_back(jump);
    }
}

void N64Recomp::LiveGenerator::emit_label(const std::string& label_name) const {
    sljit_label* label = sljit_emit_label(compiler);

    // Check if there are any pending jumps for this label and assign them if so.
    auto find_it = context->pending_jumps.find(label_name);
    if (find_it != context->pending_jumps.end()) {
        for (sljit_jump* jump : find_it->second) {
            sljit_set_label(jump, label);
        }

        // Remove the pending jumps for this label.
        context->pending_jumps.erase(find_it);
    }

    context->labels.emplace(label_name, label);
}

void N64Recomp::LiveGenerator::emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const {
    (void)jtbl;
    (void)reg;
    // Nothing to do here, the live recompiler performs a subtraction to get the switch's case.
}

void N64Recomp::LiveGenerator::emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // Make sure there's no pending jump.
    if(context->cur_branch_jump != nullptr) {
        assert(false);
        errored = true;
        return;
    }

    // Branch conditions do not allow unary ops, except for ToS64 on the first operand to indicate the branch comparison is signed.
    if(op.operands.operand_operations[0] != UnaryOpType::None && op.operands.operand_operations[0] != UnaryOpType::ToS64) {
        assert(false);
        errored = true;
        return;
    }

    if (op.operands.operand_operations[1] != UnaryOpType::None) {
        assert(false);
        errored = true;
        return;
    }

    sljit_s32 condition_type;
    bool cmp_signed = op.operands.operand_operations[0] == UnaryOpType::ToS64;
    // Comparisons need to be inverted to account for the fact that the generator is expected to generate a code block that only runs if
    // the condition is met, meaning the branch should be taken if the condition isn't met.
    switch (op.comparison) {
        case BinaryOpType::Equal:
            condition_type = SLJIT_NOT_EQUAL;
            break;
        case BinaryOpType::NotEqual:
            condition_type = SLJIT_EQUAL;
            break;
        case BinaryOpType::GreaterEq:
            if (cmp_signed) {
                condition_type = SLJIT_SIG_LESS;
            }
            else {
                condition_type = SLJIT_LESS;
            }
            break;
        case BinaryOpType::Greater:
            if (cmp_signed) {
                condition_type = SLJIT_SIG_LESS_EQUAL;
            }
            else {
                condition_type = SLJIT_LESS_EQUAL;
            }
            break;
        case BinaryOpType::LessEq:
            if (cmp_signed) {
                condition_type = SLJIT_SIG_GREATER;
            }
            else {
                condition_type = SLJIT_GREATER;
            }
            break;
        case BinaryOpType::Less:
            if (cmp_signed) {
                condition_type = SLJIT_SIG_GREATER_EQUAL;
            }
            else {
                condition_type = SLJIT_GREATER_EQUAL;
            }
            break;
        default:
            assert(false && "Invalid branch condition comparison operation!");
            errored = true;
            return;
    }
    sljit_sw src1;
    sljit_sw src1w;
    sljit_sw src2;
    sljit_sw src2w;

    get_operand_values(op.operands.operands[0], ctx, src1, src1w);
    get_operand_values(op.operands.operands[1], ctx, src2, src2w);

    // Relocations aren't valid on conditional branches.
    if(ctx.reloc_type != RelocType::R_MIPS_NONE) {
        assert(false);
        errored = true;
        return;
    }

    // Create a compare jump and track it as the pending branch jump.
    context->cur_branch_jump = sljit_emit_cmp(compiler, condition_type, src1, src1w, src2, src2w);
}

void N64Recomp::LiveGenerator::emit_branch_close() const {
    // Make sure there's a pending branch jump.
    if(context->cur_branch_jump == nullptr) {
        assert(false);
        errored = true;
        return;
    }

    // Assign a label at this point to the pending branch jump and clear it.
    sljit_set_label(context->cur_branch_jump, sljit_emit_label(compiler));
    context->cur_branch_jump = nullptr;
}

void N64Recomp::LiveGenerator::emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const {
    // Populate the switch's labels.
    std::vector<std::string> cur_labels{};
    cur_labels.resize(jtbl.entries.size());
    for (size_t i = 0; i < cur_labels.size(); i++) {
        cur_labels[i] = fmt::format("L_{:08X}", jtbl.entries[i]);
    }
    context->switch_jump_labels.emplace_back(std::move(cur_labels));

    // Allocate the jump table.
    std::unique_ptr<void* []> cur_jump_table = std::make_unique<void* []>(jtbl.entries.size());

    /// Codegen

    // Load the jump target register. The lw instruction was patched into an addiu, so this holds
    // the address of the jump table entry instead of the actual jump target.
    sljit_emit_op1(compiler, SLJIT_MOV, Registers::arithmetic_temp1, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(reg));
    // Subtract the jump table's address from the jump target to get the jump table addend.
    // Sign extend the jump table address to 64 bits so that the entire register's contents are used instead of just the lower 32 bits.
    const auto& jtbl_section = recompiler_context.sections[jtbl.section_index];
    if (jtbl_section.relocatable) {
        // Make a dummy instruction context to pass to `load_relocated_address`.
        InstructionContext dummy_context{};
        
        // Get the relocated address of the jump table.
        uint32_t section_offset = jtbl.vram - jtbl_section.ram_addr;

        // Populate the necessary fields of the dummy context and load the relocated address into temp2.
        dummy_context.reloc_section_index = jtbl.section_index;
        dummy_context.reloc_target_section_offset = section_offset;
        load_relocated_address(dummy_context, Registers::arithmetic_temp2);

        // Subtract the relocated jump table start address from the loaded address. 
        sljit_emit_op2(compiler, SLJIT_SUB, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp2, 0);
    }
    else {
        sljit_emit_op2(compiler, SLJIT_SUB, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, (sljit_sw)((int32_t)jtbl.vram));
    }
    
    // Bounds check the addend. If it's greater than or equal to the jump table size (entries * sizeof(u32)) then jump to the switch error.
    sljit_jump* switch_error_jump = sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, Registers::arithmetic_temp1, 0, SLJIT_IMM, jtbl.entries.size() * sizeof(uint32_t));
    context->switch_error_jumps.emplace_back(SwitchErrorJump{.instr_vram = jtbl.jr_vram, .jtbl_vram = jtbl.vram, .jump = switch_error_jump});

    // Multiply the jump table addend by 2 to get the addend for the real jump table. (4 bytes per entry to 8 bytes per entry).
    sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
    // Load the real jump table address.
    sljit_emit_op1(compiler, SLJIT_MOV, Registers::arithmetic_temp2, 0, SLJIT_IMM, (sljit_sw)cur_jump_table.get());
    // Load the real jump entry.
    sljit_emit_op1(compiler, SLJIT_MOV, Registers::arithmetic_temp1, 0, SLJIT_MEM2(Registers::arithmetic_temp1, Registers::arithmetic_temp2), 0);
    // Jump to the loaded entry.
    sljit_emit_ijump(compiler, SLJIT_JUMP, Registers::arithmetic_temp1, 0);

    // Move the jump table into the pending jump tables.
    context->pending_jump_tables.emplace_back(std::move(cur_jump_table));
}

void N64Recomp::LiveGenerator::emit_case(int case_index, const std::string& target_label) const {
    (void)case_index;
    (void)target_label;
    // Nothing to do here, the jump table is built in emit_switch.
}

void N64Recomp::LiveGenerator::emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const {
    (void)instr_vram;
    (void)jtbl_vram;
    // Nothing to do here, the jump table is built in emit_switch.
}

void N64Recomp::LiveGenerator::emit_switch_close() const {
    // Nothing to do here, the jump table is built in emit_switch.
}

void N64Recomp::LiveGenerator::emit_return(const Context& context, size_t func_index) const {
    (void)context;
    
    // Check if this function's return is hooked and emit the hook call if so.
    auto find_hook_it = inputs.return_func_hooks.find(func_index);
    if (find_hook_it != inputs.return_func_hooks.end()) {
        // Load rdram and ctx into R0 and R1.
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
        // Load the return hook's index into R2.
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, find_hook_it->second);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, P, W), SLJIT_IMM, sljit_sw(inputs.run_hook));
    }
    sljit_emit_return_void(compiler);
}

void N64Recomp::LiveGenerator::emit_check_fr(int fpr) const {
    (void)fpr;
    // Nothing to do here.
}

void N64Recomp::LiveGenerator::emit_check_nan(int fpr, bool is_double) const {
    (void)fpr;
    (void)is_double;
    // Nothing to do here.
}

void N64Recomp::LiveGenerator::emit_cop0_status_read(int reg) const {
    // Skip the read if the target is the zero register.
    if (reg != 0) {
        // Load ctx into R0.
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, Registers::ctx, 0);

        // Call cop0_status_read.
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(P), SLJIT_IMM, sljit_sw(inputs.cop0_status_read));

        // Store the result in the output register.
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(reg), SLJIT_R0, 0);
    }
}

void N64Recomp::LiveGenerator::emit_cop0_status_write(int reg) const {
    sljit_sw src;
    sljit_sw srcw;
    get_gpr_values(reg, src, srcw);
    
    // Load ctx and the input register value into R0 and R1
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, Registers::ctx, 0);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, src, srcw);

    // Call cop0_status_write.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P,32), SLJIT_IMM, sljit_sw(inputs.cop0_status_write));
}

void N64Recomp::LiveGenerator::emit_cop1_cs_read(int reg) const {
    // Skip the read if the target is the zero register.
    if (reg != 0) {
        sljit_sw dst;
        sljit_sw dstw;
        get_gpr_values(reg, dst, dstw);

        // Call get_cop1_cs.
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS0(32), SLJIT_IMM, sljit_sw(get_cop1_cs));

        // Sign extend the result into a temp register.
        sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, SLJIT_RETURN_REG, 0);

        // Move the sign extended result into the destination.
        sljit_emit_op1(compiler, SLJIT_MOV, dst, dstw, Registers::arithmetic_temp1, 0);
    }
}

void N64Recomp::LiveGenerator::emit_cop1_cs_write(int reg) const {
    sljit_sw src;
    sljit_sw srcw;
    get_gpr_values(reg, src, srcw);

    // Load the input register value into R0.
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, src, srcw);

    // Call set_cop1_cs.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(32), SLJIT_IMM, sljit_sw(set_cop1_cs));
}

void N64Recomp::LiveGenerator::emit_muldiv(InstrId instr_id, int reg1, int reg2) const {
    sljit_sw src1;
    sljit_sw src1w;
    sljit_sw src2;
    sljit_sw src2w;
    get_gpr_values(reg1, src1, src1w);
    get_gpr_values(reg2, src2, src2w);
    
    auto do_mul32_op = [src1, src1w, src2, src2w, this](bool is_signed) {
        // Load the two inputs into the multiplication input registers (R0/R1).
        if (is_signed) {
            // 32-bit signed multiplication is really 64 bits * 35 bits, so load accordingly.
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, src1, src1w); 

            // Sign extend to 35 bits by shifting left by 64 - 35 and then shifting right by the same amount.
            sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R1, 0, src2, src2w, SLJIT_IMM, 64 - 35);
            sljit_emit_op2(compiler, SLJIT_ASHR, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 64 - 35);
        }
        else {
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_R0, 0, src1, src1w);
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_R1, 0, src2, src2w);
        }

        // Perform the multiplication.
        sljit_emit_op0(compiler, is_signed ? SLJIT_LMUL_SW : SLJIT_LMUL_UW);

        // Move the results into hi and lo with sign extension.
        sljit_emit_op2(compiler, SLJIT_ASHR, Registers::hi, 0, SLJIT_R0, 0, SLJIT_IMM, 32);
        sljit_emit_op1(compiler, SLJIT_MOV_S32, Registers::lo, 0, SLJIT_R0, 0);
    };
    
    auto do_mul64_op = [src1, src1w, src2, src2w, this](bool is_signed) {
        // Load the two inputs into the multiplication input registers (R0/R1).
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, src1, src1w); 
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, src2, src2w);

        // Perform the multiplication.
        sljit_emit_op0(compiler, is_signed ? SLJIT_LMUL_SW : SLJIT_LMUL_UW);

        // Move the results into hi and lo.
        sljit_emit_op1(compiler, SLJIT_MOV, Registers::hi, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, Registers::lo, 0, SLJIT_R0, 0);
    };
    
    auto do_div_op = [src1, src1w, src2, src2w, this](bool doubleword, bool is_signed) {
        // Pick the division opcode based on the bit width and signedness.
        // Note that the 64-bit division opcode is used for 32-bit signed division to match hardware behavior and prevent overflow.
        sljit_sw div_opcode = doubleword ?
            (is_signed ? SLJIT_DIVMOD_SW : SLJIT_DIVMOD_UW) :
            (is_signed ? SLJIT_DIVMOD_SW : SLJIT_DIVMOD_U32);

        // Pick the move opcode to use for loading the operands.
        sljit_sw load_opcode = doubleword ? SLJIT_MOV :
            (is_signed ? SLJIT_MOV_S32 : SLJIT_MOV_U32);

        // Pick the move opcode to use for saving the results.
        sljit_sw save_opcode = doubleword ? SLJIT_MOV : SLJIT_MOV_S32;

        // Load the two inputs into R0 and R1 (the numerator and denominator).
        sljit_emit_op1(compiler, load_opcode, SLJIT_R0, 0, src1, src1w); 

        // TODO figure out 32-bit signed division behavior when inputs aren't properly sign extended.
        // if (!doubleword && is_signed) {
        //     // Sign extend to 35 bits by shifting left by 64 - 35 and then shifting right by the same amount.
        //     sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R1, 0, src2, src2w, SLJIT_IMM, 64 - 35);
        //     sljit_emit_op2(compiler, SLJIT_ASHR, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 64 - 35);
        // }
        // else {
            sljit_emit_op1(compiler, load_opcode, SLJIT_R1, 0, src2, src2w);
        // }

        // Prevent overflow on 64-bit signed division.
        if (doubleword && is_signed) {
            // If the numerator is INT64_MIN and the denominator is -1, an overflow will occur. To prevent an exception and
            // behave as the original hardware would, check if either of those conditions are false.
            // If neither condition is false (i.e. both are true), set the denominator to 1.

            // Xor the numerator with INT64_MIN. This will be zero if they're equal.
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp1, 0, SLJIT_IMM, sljit_sw(INT64_MIN));

            // Invert the denominator. This will be zero if it's -1.
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::arithmetic_temp4, 0, Registers::arithmetic_temp2, 0, SLJIT_IMM, sljit_sw(-1)); 

            // Or the results of the previous two calculations and set the zero flag. This will be zero if both conditions were met.
            sljit_emit_op2(compiler, SLJIT_OR | SLJIT_SET_Z, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp3, 0, Registers::arithmetic_temp4, 0);

            // If the zero flag is 0, meaning both conditions were true, replace the denominator with 1.
            // i.e. conditionally move an immediate of 1 into arithmetic temp 2 if the zero flag is 0.
            sljit_emit_select(compiler, SLJIT_ZERO, SLJIT_R1, SLJIT_IMM, 1, SLJIT_R1);
        }

        // If the denominator is 0, skip the division and jump the special handling for that case.
        // Branch past the division if the divisor is 0.
        sljit_jump* jump_skip_division = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);// sljit_emit_jump(compiler, SLJIT_ZERO);

        // Perform the division.
        sljit_emit_op0(compiler, div_opcode);

        // Extract the remainder and quotient into the high and low registers respectively.
        sljit_emit_op1(compiler, save_opcode, Registers::hi, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, save_opcode, Registers::lo, 0, SLJIT_R0, 0);

        // Jump to the end of this routine.
        sljit_jump* jump_to_end = sljit_emit_jump(compiler, SLJIT_JUMP);

        // Emit a label and set it as the target of the jump if the denominator was zero.
        sljit_label* after_division = sljit_emit_label(compiler);
        sljit_set_label(jump_skip_division, after_division);

        // Move the numerator into hi.
        sljit_emit_op1(compiler, save_opcode, Registers::hi, 0, SLJIT_R0, 0);

        if (is_signed) {
            // Calculate the negative signum of the numerator and place it in lo.
            // neg_signum = ((int64_t)(~x) >> (bit width - 1)) | 1
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::lo, 0, SLJIT_R0, 0, SLJIT_IMM, sljit_sw(-1));
            sljit_emit_op2(compiler, SLJIT_ASHR, Registers::lo, 0, Registers::lo, 0, SLJIT_IMM, 64 - 1);
            sljit_emit_op2(compiler, SLJIT_OR, Registers::lo, 0, Registers::lo, 0, SLJIT_IMM, 1);
        }
        else {
            // Move -1 into lo.
            sljit_emit_op1(compiler, SLJIT_MOV, Registers::lo, 0, SLJIT_IMM, sljit_sw(-1));
        }

        // Emit a label and set it as the target of the jump after the divison.
        sljit_label* end_label = sljit_emit_label(compiler);
        sljit_set_label(jump_to_end, end_label);
    };
    

    switch (instr_id) {
        case InstrId::cpu_mult:
            do_mul32_op(true);
            break;
        case InstrId::cpu_multu:
            do_mul32_op(false);
            break;
        case InstrId::cpu_dmult:
            do_mul64_op(true);
            break;
        case InstrId::cpu_dmultu:
            do_mul64_op(false);
            break;
        case InstrId::cpu_div:
            do_div_op(false, true);
            break;
        case InstrId::cpu_divu:
            do_div_op(false, false);
            break;
        case InstrId::cpu_ddiv:
            do_div_op(true, true);
            break;
        case InstrId::cpu_ddivu:
            do_div_op(true, false);
            break;
        default:
            assert(false && "Invalid mul/div instruction id!");
            break;
    }
}

void N64Recomp::LiveGenerator::emit_syscall(uint32_t instr_vram) const {
    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
    // Load the vram into R2.
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R2, 0, SLJIT_IMM, instr_vram);
    // Call syscall_handler.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, P, 32), SLJIT_IMM, sljit_sw(inputs.syscall_handler));
}

void N64Recomp::LiveGenerator::emit_do_break(uint32_t instr_vram) const {
    // Load the vram into R0.
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_IMM, instr_vram);
    // Call do_break.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(32), SLJIT_IMM, sljit_sw(inputs.do_break));
}

void N64Recomp::LiveGenerator::emit_pause_self() const {
    // Load rdram into R0.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    // Call pause_self.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(P), SLJIT_IMM, sljit_sw(inputs.pause_self));
}

void N64Recomp::LiveGenerator::emit_trigger_event(uint32_t event_index) const {
    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, rdram_offset);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
    // Load the global event index into R2.
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R2, 0, SLJIT_IMM, event_index + inputs.base_event_index);
    // Call trigger_event.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P,P,32), SLJIT_IMM, sljit_sw(inputs.trigger_event));
}

void N64Recomp::LiveGenerator::emit_comment(const std::string& comment) const {
    (void)comment;
    // Nothing to do here.
}

bool N64Recomp::recompile_function_live(LiveGenerator& generator, const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    return recompile_function_custom(generator, context, function_index, output_file, static_funcs_out, tag_reference_relocs);
}

