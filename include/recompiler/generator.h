#ifndef __GENERATOR_H__
#define __GENERATOR_H__

#include "recompiler/context.h"
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
        virtual void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const = 0;
        virtual void process_store_op(const StoreOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_function_start(const std::string& function_name, size_t func_index) const = 0;
        virtual void emit_function_end() const = 0;
        virtual void emit_function_call_lookup(uint32_t addr) const = 0;
        virtual void emit_function_call_by_register(int reg) const = 0;
        // target_section_offset can each be deduced from symbol_index if the full context is available,
        // but for live recompilation the reference symbol list is unavailable so it's still provided.
        virtual void emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const = 0;
        virtual void emit_function_call(const Context& context, size_t function_index) const = 0;
        virtual void emit_goto(const std::string& target) const = 0;
        virtual void emit_label(const std::string& label_name) const = 0;
        virtual void emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const = 0;
        virtual void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const = 0;
        virtual void emit_branch_close() const = 0;
        virtual void emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const = 0;
        virtual void emit_case(int case_index, const std::string& target_label) const = 0;
        virtual void emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const = 0;
        virtual void emit_switch_close() const = 0;
        virtual void emit_return() const = 0;
        virtual void emit_check_fr(int fpr) const = 0;
        virtual void emit_check_nan(int fpr, bool is_double) const = 0;
        virtual void emit_cop0_status_read(int reg) const = 0;
        virtual void emit_cop0_status_write(int reg) const = 0;
        virtual void emit_cop1_cs_read(int reg) const = 0;
        virtual void emit_cop1_cs_write(int reg) const = 0;
        virtual void emit_muldiv(InstrId instr_id, int reg1, int reg2) const = 0;
        virtual void emit_syscall(uint32_t instr_vram) const = 0;
        virtual void emit_do_break(uint32_t instr_vram) const = 0;
        virtual void emit_pause_self() const = 0;
        virtual void emit_trigger_event(uint32_t event_index) const = 0;
        virtual void emit_comment(const std::string& comment) const = 0;
    };

    class CGenerator final : Generator {
    public:
        CGenerator(std::ostream& output_file) : output_file(output_file) {};
        void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_function_start(const std::string& function_name, size_t func_index) const final;
        void emit_function_end() const final;
        void emit_function_call_lookup(uint32_t addr) const final;
        void emit_function_call_by_register(int reg) const final;
        void emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const final;
        void emit_function_call(const Context& context, size_t function_index) const final;
        void emit_goto(const std::string& target) const final;
        void emit_label(const std::string& label_name) const final;
        void emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const final;
        void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close() const final;
        void emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const final;
        void emit_case(int case_index, const std::string& target_label) const final;
        void emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const final;
        void emit_switch_close() const final;
        void emit_return() const final;
        void emit_check_fr(int fpr) const final;
        void emit_check_nan(int fpr, bool is_double) const final;
        void emit_cop0_status_read(int reg) const final;
        void emit_cop0_status_write(int reg) const final;
        void emit_cop1_cs_read(int reg) const final;
        void emit_cop1_cs_write(int reg) const final;
        void emit_muldiv(InstrId instr_id, int reg1, int reg2) const final;
        void emit_syscall(uint32_t instr_vram) const final;
        void emit_do_break(uint32_t instr_vram) const final;
        void emit_pause_self() const final;
        void emit_trigger_event(uint32_t event_index) const final;
        void emit_comment(const std::string& comment) const final;
    private:
        void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const;
        void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const;
        void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const;
        std::ostream& output_file;
    };
}

#endif
