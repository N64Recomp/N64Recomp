#include <cassert>
#include <fstream>
#include <unordered_map>

#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/live_recompiler.h"
#include "recomp.h"

#include "sljitLir.h"

void N64Recomp::live_recompiler_init() {
    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;
}

namespace Registers {
    constexpr int rdram = SLJIT_S0; // stores (rdram - 0xFFFFFFFF80000000)
    constexpr int ctx = SLJIT_S1; // stores ctx
    constexpr int c1cs = SLJIT_S2; // stores ctx
    constexpr int hi = SLJIT_S3; // stores ctx
    constexpr int lo = SLJIT_S4; // stores ctx
    constexpr int arithmetic_temp1 = SLJIT_R0;
    constexpr int arithmetic_temp2 = SLJIT_R1;
    constexpr int arithmetic_temp3 = SLJIT_R2;
    constexpr int arithmetic_temp4 = SLJIT_R3;
    constexpr int float_temp = SLJIT_FR0;
}

struct InnerCall {
    size_t target_func_index;
    sljit_jump* jump;
};

struct ReferenceSymbolCall {
    uint16_t reference;
};

struct N64Recomp::LiveGeneratorContext {
    std::unordered_map<std::string, sljit_label*> labels;
    std::unordered_map<std::string, std::vector<sljit_jump*>> pending_jumps;
    std::vector<sljit_label*> func_labels;
    std::vector<InnerCall> inner_calls;
    sljit_jump* cur_branch_jump;
};

N64Recomp::LiveGenerator::LiveGenerator(size_t num_funcs, const LiveGeneratorInputs& inputs) : compiler(compiler), inputs(inputs) {
    compiler = sljit_create_compiler(NULL);
    context = std::make_unique<LiveGeneratorContext>();
    context->func_labels.resize(num_funcs);
}

N64Recomp::LiveGenerator::~LiveGenerator() {
    if (compiler != nullptr) {
        sljit_free_compiler(compiler);
        compiler = nullptr;
    }
}

N64Recomp::LiveGeneratorOutput N64Recomp::LiveGenerator::finish() {
    LiveGeneratorOutput ret{};
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

    sljit_free_compiler(compiler);
    compiler = nullptr;

    return ret;
}

N64Recomp::LiveGeneratorOutput::~LiveGeneratorOutput() {
    if (code != nullptr) {
        sljit_free_code(code, nullptr);
    }
    for (const char* literal : string_literals) {
        delete[] literal;
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

void get_operand_values(N64Recomp::Operand operand, const N64Recomp::InstructionContext& context, sljit_sw& out, sljit_sw& outw) {
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
            break;
        case Operand::FsU32H:
            assert(false);
            break;
        case Operand::FtU32H:
            assert(false);
            break;
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
            //if (context.reloc_type != N64Recomp::RelocType::R_MIPS_NONE) {
            //    assert(false);
            //}
            //else {
                out = SLJIT_IMM;
                outw = (sljit_sw)(uint16_t)context.imm16;
            //}
            break;
        case Operand::ImmS16:
            //if (context.reloc_type != N64Recomp::RelocType::R_MIPS_NONE) {
            //    assert(false);
            //}
            //else {
                out = SLJIT_IMM;
                outw = (sljit_sw)(int16_t)context.imm16;
            //}
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

    bool failed = false;    
    sljit_sw dst;
    sljit_sw dstw;
    sljit_sw src1;
    sljit_sw src1w;
    sljit_sw src2;
    sljit_sw src2w;
    get_operand_values(op.output, ctx, dst, dstw);
    get_operand_values(op.operands.operands[0], ctx, src1, src1w);
    get_operand_values(op.operands.operands[1], ctx, src2, src2w);

    // TODO validate that the unary ops are valid for the current binary op.
    assert(op.operands.operand_operations[0] == UnaryOpType::None ||
           op.operands.operand_operations[0] == UnaryOpType::ToU64 ||
           op.operands.operand_operations[0] == UnaryOpType::ToS64 ||
           op.operands.operand_operations[0] == UnaryOpType::ToU32);
    
    assert(op.operands.operand_operations[1] == UnaryOpType::None ||
           op.operands.operand_operations[1] == UnaryOpType::Mask5 || // Only for 32-bit shifts
           op.operands.operand_operations[1] == UnaryOpType::Mask6); // Only for 64-bit shifts

    bool cmp_unsigned = op.operands.operand_operations[0] != UnaryOpType::ToS64;

    auto sign_extend_and_store = [dst, dstw, this]() {
        // Sign extend the result.
        sljit_emit_op1(this->compiler, SLJIT_MOV_S32, Registers::arithmetic_temp1, 0, Registers::arithmetic_temp1, 0);
        // Store the result back into the context.
        sljit_emit_op1(this->compiler, SLJIT_MOV_P, dst, dstw, Registers::arithmetic_temp1, 0);
    };

    auto do_op32 = [dst, dstw, src1, src1w, src2, src2w, this, &sign_extend_and_store](sljit_s32 op) {
        sljit_emit_op2(this->compiler, op, Registers::arithmetic_temp1, 0, src1, src1w, src2, src2w);
        sign_extend_and_store();
    };

    auto do_op64 = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op) {
        sljit_emit_op2(this->compiler, op, dst, dstw, src1, src1w, src2, src2w);
    };

    auto do_float_op = [dst, dstw, src1, src1w, src2, src2w, this](sljit_s32 op) {
        sljit_emit_fop2(this->compiler, op, dst, dstw, src1, src1w, src2, src2w);
    };

    auto do_load_op = [dst, dstw, src1, src1w, src2, src2w, &ctx, &failed, this](sljit_s32 op, int address_xor) {
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

    auto do_compare_op = [cmp_unsigned, dst, dstw, src1, src1w, src2, src2w, &ctx, &failed, this](sljit_s32 op_unsigned, sljit_s32 op_signed) {
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
            break;
    }

    assert(!failed);
}

void N64Recomp::LiveGenerator::process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const {
    // Skip instructions that output to $zero
    if (op.output == Operand::Rd && ctx.rd == 0) {
        return;
    }
    if (op.output == Operand::Rt && ctx.rt == 0) {
        return;
    }
    if (op.output == Operand::Rs && ctx.rs == 0) {
        return;
    }
    
    sljit_sw dst;
    sljit_sw dstw;
    sljit_sw src;
    sljit_sw srcw;
    get_operand_values(op.output, ctx, dst, dstw);
    get_operand_values(op.input, ctx, src, srcw);

    sljit_s32 jit_op;

    bool failed = false;
    bool float_op = false;

    switch (op.operation) {
        case UnaryOpType::Lui:
            if (src != SLJIT_IMM) {
                failed = true;
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
        case UnaryOpType::None:
            jit_op = SLJIT_MOV;
            break;
        default:
            assert(false);
            return;
    }

    if (float_op) {
        sljit_emit_fop1(compiler, jit_op, dst, dstw, src, srcw);
    }
    else {
        sljit_emit_op1(compiler, jit_op, dst, dstw, src, srcw);
    }

    assert(!failed);
}

void N64Recomp::LiveGenerator::process_store_op(const StoreOp& op, const InstructionContext& ctx) const {
    sljit_sw src;
    sljit_sw srcw;
    sljit_sw imm = (sljit_sw)(int16_t)ctx.imm16;

    get_operand_values(op.value_input, ctx, src, srcw);

    // Add the base register (rs) and the immediate to get the address and store it in the arithemtic temp.
    sljit_emit_op2(compiler, SLJIT_ADD, Registers::arithmetic_temp1, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(ctx.rs), SLJIT_IMM, imm);

    switch (op.type) {
        case StoreOpType::SD:
        case StoreOpType::SDC1:        
            // Rotate the arithmetic temp by 32 bits to swap the words and move it into the destination.
            sljit_emit_op2(compiler, SLJIT_ROTL, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw, SLJIT_IMM, 32);
            break;
        case StoreOpType::SDL:
            assert(false);
            break;
        case StoreOpType::SDR:
            assert(false);
            break;
        case StoreOpType::SW:
        case StoreOpType::SWC1:
            // store the 32-bit value at address + rdram
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_MEM2(Registers::rdram, Registers::arithmetic_temp1), 0, src, srcw);
            break;
        case StoreOpType::SWL:
            assert(false);
            break;
        case StoreOpType::SWR:
            assert(false);
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
    context->func_labels[func_index] = sljit_emit_label(compiler);
    sljit_emit_enter(compiler, 0, SLJIT_ARGS2V(P, P), 4, 5, 0);
    sljit_emit_op2(compiler, SLJIT_SUB, Registers::rdram, 0, Registers::rdram, 0, SLJIT_IMM, 0xFFFFFFFF80000000);
}

void N64Recomp::LiveGenerator::emit_function_end() const {
    // Nothing to do here.
}

void N64Recomp::LiveGenerator::emit_function_call_lookup(uint32_t addr) const {
    // Load the address immediate into the first argument. 
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_IMM, int32_t(addr));
    
    // Call get_function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, 32), SLJIT_IMM, sljit_sw(inputs.get_function));
    
    // Copy the return value into R2 so that it can be used for icall
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);
    
    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, 0xFFFFFFFF80000000);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);

    // Call the function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P), SLJIT_R2, 0);
}

void N64Recomp::LiveGenerator::emit_function_call_by_register(int reg) const {
    // Load the register's value into the first argument. 
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(Registers::ctx), get_gpr_context_offset(reg));

    // Call get_function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(P, 32), SLJIT_IMM, sljit_sw(inputs.get_function));

    // Copy the return value into R2 so that it can be used for icall
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);

    // Load rdram and ctx into R0 and R1.
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, 0xFFFFFFFF80000000);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);

    // Call the function.
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P), SLJIT_R2, 0);
}

void N64Recomp::LiveGenerator::emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index) const {
    const N64Recomp::ReferenceSymbol& sym = context.get_reference_symbol(section_index, symbol_index);
    assert(false);
}

void N64Recomp::LiveGenerator::emit_function_call(const Context& recompiler_context, size_t function_index) const {
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, Registers::rdram, 0, SLJIT_IMM, 0xFFFFFFFF80000000);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, Registers::ctx, 0);
    sljit_jump* call_jump = sljit_emit_call(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P));
    context->inner_calls.emplace_back(InnerCall{ .target_func_index = function_index, .jump = call_jump });
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

void N64Recomp::LiveGenerator::emit_variable_declaration(const std::string& var_name, int reg) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const {
    // Make sure there's no pending jump.
    assert(context->cur_branch_jump == nullptr);

    // Branch conditions do not allow unary ops, except for ToS64 on the first operand to indicate the branch comparison is signed.
    assert(op.operands.operand_operations[0] == UnaryOpType::None || op.operands.operand_operations[0] == UnaryOpType::ToS64);
    assert(op.operands.operand_operations[1] == UnaryOpType::None);

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
            assert(false);
    }
    sljit_sw src1;
    sljit_sw src1w;
    sljit_sw src2;
    sljit_sw src2w;

    get_operand_values(op.operands.operands[0], ctx, src1, src1w);
    get_operand_values(op.operands.operands[1], ctx, src2, src2w);

    // Create a compare jump and track it as the pending branch jump.
    context->cur_branch_jump = sljit_emit_cmp(compiler, condition_type, src1, src1w, src2, src2w);
}

void N64Recomp::LiveGenerator::emit_branch_close() const {
    // Make sure there's a pending branch jump.
    assert(context->cur_branch_jump != nullptr);

    // Assign a label at this point to the pending branch jump and clear it.
    sljit_set_label(context->cur_branch_jump, sljit_emit_label(compiler));
    context->cur_branch_jump = nullptr;
}

void N64Recomp::LiveGenerator::emit_switch(const std::string& jump_variable, int shift_amount) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_case(int case_index, const std::string& target_label) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_switch_close() const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_return() const {
    sljit_emit_return_void(compiler);
}

void N64Recomp::LiveGenerator::emit_check_fr(int fpr) const {
    // Nothing to do here.
}

void N64Recomp::LiveGenerator::emit_check_nan(int fpr, bool is_double) const {
    // Nothing to do here.
}

void N64Recomp::LiveGenerator::emit_cop0_status_read(int reg) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_cop0_status_write(int reg) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_cop1_cs_read(int reg) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_cop1_cs_write(int reg) const {
    assert(false);
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
        // Set the zero flag if the denominator is zero by AND'ing it with itself.
        sljit_emit_op2u(compiler, SLJIT_AND | SLJIT_SET_Z, SLJIT_R1, 0, SLJIT_R1, 0);

        // Branch past the division if the zero flag is 0.
        sljit_jump* jump_skip_division = sljit_emit_jump(compiler, SLJIT_ZERO);

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
        sljit_emit_op1(compiler, SLJIT_MOV, Registers::hi, 0, SLJIT_R0, 0);

        if (is_signed) {
            // Calculate the negative signum of the numerator and place it in lo.
            // neg_signum = ((int64_t)(~x) >> (bit width - 1)) | 1
            sljit_emit_op2(compiler, SLJIT_XOR, Registers::lo, 0, SLJIT_R0, 0, SLJIT_IMM, sljit_sw(-1));
            sljit_emit_op2(compiler, SLJIT_ASHR, Registers::lo, 0, Registers::lo, 0, SLJIT_IMM, 64 - 1);
            sljit_emit_op2(compiler, SLJIT_OR, Registers::lo, 0, Registers::lo, 0, SLJIT_IMM, 1);
        }
        else {
            // Move -1 into lo.
            sljit_emit_op1(compiler, SLJIT_MOV, Registers::lo, 0, SLJIT_IMM, -1);
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
            assert(false);
            break;
    }
}

void N64Recomp::LiveGenerator::emit_syscall(uint32_t instr_vram) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_do_break(uint32_t instr_vram) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_pause_self() const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_trigger_event(size_t event_index) const {
    assert(false);
}

void N64Recomp::LiveGenerator::emit_comment(const std::string& comment) const {
    // Nothing to do here.
}

bool N64Recomp::recompile_function_live(LiveGenerator& generator, const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs) {
    return recompile_function_custom(generator, context, function_index, output_file, static_funcs_out, tag_reference_relocs);
}

