#ifndef __LIVE_RECOMPILER_H__
#define __LIVE_RECOMPILER_H__

#include "recompiler/generator.h"
#include "recomp.h"

struct sljit_compiler;

namespace N64Recomp {
    struct LiveGeneratorContext;
    struct LiveGeneratorOutput {
        LiveGeneratorOutput() = default;
        LiveGeneratorOutput(const LiveGeneratorOutput& rhs) = delete;
        LiveGeneratorOutput(LiveGeneratorOutput&& rhs) { *this = std::move(rhs); }
        LiveGeneratorOutput& operator=(const LiveGeneratorOutput& rhs) = delete;
        LiveGeneratorOutput& operator=(LiveGeneratorOutput&& rhs) {
            good = rhs.good;
            functions = std::move(rhs.functions);
            code = rhs.code;
            code_size = rhs.code_size;

            rhs.good = false;
            rhs.code = nullptr;
            rhs.code_size = 0;

            return *this;
        }
        ~LiveGeneratorOutput();
        bool good = false;
        std::vector<recomp_func_t*> functions;
        void* code;
        size_t code_size;
    };
    class LiveGenerator final : public Generator {
    public:
        LiveGenerator(size_t num_funcs);
        ~LiveGenerator();
        LiveGeneratorOutput finish();
        void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_function_start(const std::string& function_name, size_t func_index) const final;
        void emit_function_end() const final;
        void emit_function_call_lookup(uint32_t addr) const final;
        void emit_function_call_by_register(int reg) const final;
        void emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index) const final;
        void emit_function_call(const Context& context, size_t function_index) const final;
        void emit_goto(const std::string& target) const final;
        void emit_label(const std::string& label_name) const final;
        void emit_variable_declaration(const std::string& var_name, int reg) const final;
        void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close() const final;
        void emit_switch(const std::string& jump_variable, int shift_amount) const final;
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
        void emit_trigger_event(size_t event_index) const final;
        void emit_comment(const std::string& comment) const final;
    private:
        void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const;
        void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const;
        void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const;
        sljit_compiler* compiler;
        mutable std::unique_ptr<LiveGeneratorContext> context;
    };

    void live_recompiler_init();
    bool recompile_function_live(LiveGenerator& generator, const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs);
}

#endif