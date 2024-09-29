#ifndef __GENERATOR_H__
#define __GENERATOR_H__

#include "n64recomp.h"
#include "operations.h"

namespace N64Recomp {
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

        bool reloc_tag_as_reference;
        RelocType reloc_type;
        uint32_t reloc_section_index;
        uint32_t reloc_target_section_offset;
    };

    class Generator {
    public:
        virtual void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_function_start(std::ostream& output_file, const std::string& function_name) const = 0;
        virtual void emit_function_end(std::ostream& output_file) const = 0;
        virtual void emit_function_call_lookup(std::ostream& output_file, uint32_t addr) const = 0;
        virtual void emit_function_call_by_register(std::ostream& output_file, int reg) const = 0;
        virtual void emit_function_call_by_name(std::ostream& output_file, const std::string& func_name) const = 0;
        virtual void emit_goto(std::ostream& output_file, const std::string& target) const = 0;
        virtual void emit_label(std::ostream& output_file, const std::string& label_name) const = 0;
        virtual void emit_variable_declaration(std::ostream& output_file, const std::string& var_name, int reg) const = 0;
        virtual void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_branch_close(std::ostream& output_file) const = 0;
        virtual void emit_switch(std::ostream& output_file, const std::string& jump_variable, int shift_amount) const = 0;
        virtual void emit_case(std::ostream& output_file, int case_index, const std::string& target_label) const = 0;
        virtual void emit_switch_error(std::ostream& output_file, uint32_t instr_vram, uint32_t jtbl_vram) const = 0;
        virtual void emit_switch_close(std::ostream& output_file) const = 0;
        virtual void emit_return(std::ostream& output_file) const = 0;
        virtual void emit_check_fr(std::ostream& output_file, int fpr) const = 0;
        virtual void emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const = 0;
        virtual void emit_cop0_status_read(std::ostream& output_file, int reg) const = 0;
        virtual void emit_cop0_status_write(std::ostream& output_file, int reg) const = 0;
        virtual void emit_cop1_cs_read(std::ostream& output_file, int reg) const = 0;
        virtual void emit_cop1_cs_write(std::ostream& output_file, int reg) const = 0;
        virtual void emit_muldiv(std::ostream& output_file, InstrId instr_id, int reg1, int reg2) const = 0;
        virtual void emit_syscall(std::ostream& output_file, uint32_t instr_vram) const = 0;
        virtual void emit_do_break(std::ostream& output_file, uint32_t instr_vram) const = 0;
        virtual void emit_pause_self(std::ostream& output_file) const = 0;
        virtual void emit_trigger_event(std::ostream& output_file, size_t event_index) const = 0;
        virtual void emit_comment(std::ostream& output_file, const std::string& comment) const = 0;
    };

    class CGenerator final : Generator {
    public:
        CGenerator() = default;
        void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_function_start(std::ostream& output_file, const std::string& function_name) const final;
        void emit_function_end(std::ostream& output_file) const final;
        void emit_function_call_lookup(std::ostream& output_file, uint32_t addr) const final;
        void emit_function_call_by_register(std::ostream& output_file, int reg) const final;
        void emit_function_call_by_name(std::ostream& output_file, const std::string& func_name) const final;
        void emit_goto(std::ostream& output_file, const std::string& target) const final;
        void emit_label(std::ostream& output_file, const std::string& label_name) const final;
        void emit_variable_declaration(std::ostream& output_file, const std::string& var_name, int reg) const final;
        void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close(std::ostream& output_file) const final;
        void emit_switch(std::ostream& output_file, const std::string& jump_variable, int shift_amount) const final;
        void emit_case(std::ostream& output_file, int case_index, const std::string& target_label) const final;
        void emit_switch_error(std::ostream& output_file, uint32_t instr_vram, uint32_t jtbl_vram) const final;
        void emit_switch_close(std::ostream& output_file) const final;
        void emit_return(std::ostream& output_file) const final;
        void emit_check_fr(std::ostream& output_file, int fpr) const final;
        void emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const final;
        void emit_cop0_status_read(std::ostream& output_file, int reg) const final;
        void emit_cop0_status_write(std::ostream& output_file, int reg) const final;
        void emit_cop1_cs_read(std::ostream& output_file, int reg) const final;
        void emit_cop1_cs_write(std::ostream& output_file, int reg) const final;
        void emit_muldiv(std::ostream& output_file, InstrId instr_id, int reg1, int reg2) const final;
        void emit_syscall(std::ostream& output_file, uint32_t instr_vram) const final;
        void emit_do_break(std::ostream& output_file, uint32_t instr_vram) const final;
        void emit_pause_self(std::ostream& output_file) const final;
        void emit_trigger_event(std::ostream& output_file, size_t event_index) const final;
        void emit_comment(std::ostream& output_file, const std::string& comment) const final;
    private:
        void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const;
        void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const;
        void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const;
    };

    class LuajitGenerator final : Generator {
    public:
        LuajitGenerator() = default;
        void process_binary_op(std::ostream& output_file, const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(std::ostream& output_file, const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(std::ostream& output_file, const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_function_start(std::ostream& output_file, const std::string& function_name) const final;
        void emit_function_end(std::ostream& output_file) const final;
        void emit_function_call_lookup(std::ostream& output_file, uint32_t addr) const final;
        void emit_function_call_by_register(std::ostream& output_file, int reg) const final;
        void emit_function_call_by_name(std::ostream& output_file, const std::string& func_name) const final;
        void emit_goto(std::ostream& output_file, const std::string& target) const final;
        void emit_label(std::ostream& output_file, const std::string& label_name) const final;
        void emit_variable_declaration(std::ostream& output_file, const std::string& var_name, int reg) const final;
        void emit_branch_condition(std::ostream& output_file, const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close(std::ostream& output_file) const final;
        void emit_switch(std::ostream& output_file, const std::string& jump_variable, int shift_amount) const final;
        void emit_case(std::ostream& output_file, int case_index, const std::string& target_label) const final;
        void emit_switch_error(std::ostream& output_file, uint32_t instr_vram, uint32_t jtbl_vram) const final;
        void emit_switch_close(std::ostream& output_file) const final;
        void emit_return(std::ostream& output_file) const final;
        void emit_check_fr(std::ostream& output_file, int fpr) const final;
        void emit_check_nan(std::ostream& output_file, int fpr, bool is_double) const final;
        void emit_cop0_status_read(std::ostream& output_file, int reg) const final;
        void emit_cop0_status_write(std::ostream& output_file, int reg) const final;
        void emit_cop1_cs_read(std::ostream& output_file, int reg) const final;
        void emit_cop1_cs_write(std::ostream& output_file, int reg) const final;
        void emit_muldiv(std::ostream& output_file, InstrId instr_id, int reg1, int reg2) const final;
        void emit_syscall(std::ostream& output_file, uint32_t instr_vram) const final;
        void emit_do_break(std::ostream& output_file, uint32_t instr_vram) const final;
        void emit_pause_self(std::ostream& output_file) const final;
        void emit_trigger_event(std::ostream& output_file, size_t event_index) const final;
        void emit_comment(std::ostream& output_file, const std::string& comment) const final;
    private:
    };
}

#endif
