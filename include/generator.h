#ifndef __GENERATOR_H__
#define __GENERATOR_H__

#include "recomp_port.h"
#include "operations.h"

namespace RecompPort {
    struct InstructionContext {
        int rd;
        int rs;
        int rt;
        int sa;

        int fd;
        int fs;
        int ft;

        int cop1_cs;

        uint16_t imm16;

        RelocType reloc_type;
        uint32_t reloc_section_index;
        uint32_t reloc_target_section_offset;
    };

    class Generator {
    public:
        virtual void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_branch_close(std::ostream& output_file) const = 0;
        virtual void emit_check_fr(std::ostream& output_file, int fpr) const = 0;
        virtual void emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const = 0;
    };

    class CGenerator final : Generator {
    public:
        CGenerator() = default;
        void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close(std::ostream& output_file) const final;
        void emit_check_fr(std::ostream& output_file, int fpr) const final;
        void emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const final;
    private:
        void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const;
        void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const;
        void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const;
    };
}

#endif
