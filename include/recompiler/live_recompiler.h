#ifndef __LIVE_RECOMPILER_H__
#define __LIVE_RECOMPILER_H__

#include <unordered_map>
#include "recompiler/generator.h"
#include "recomp.h"

struct sljit_compiler;

namespace N64Recomp {
    struct LiveGeneratorContext;
    struct ReferenceJumpDetails {
        uint16_t section;
        uint32_t section_offset;
    };
    struct LiveGeneratorOutput {
        LiveGeneratorOutput() = default;
        LiveGeneratorOutput(const LiveGeneratorOutput& rhs) = delete;
        LiveGeneratorOutput(LiveGeneratorOutput&& rhs) { *this = std::move(rhs); }
        LiveGeneratorOutput& operator=(const LiveGeneratorOutput& rhs) = delete;
        LiveGeneratorOutput& operator=(LiveGeneratorOutput&& rhs) {
            good = rhs.good;
            string_literals = std::move(rhs.string_literals);
            jump_tables = std::move(rhs.jump_tables);
            code = rhs.code;
            code_size = rhs.code_size;
            functions = std::move(rhs.functions);
            reference_symbol_jumps = std::move(rhs.reference_symbol_jumps);
            import_jumps_by_index = std::move(rhs.import_jumps_by_index);
            executable_offset = rhs.executable_offset;

            rhs.good = false;
            rhs.code = nullptr;
            rhs.code_size = 0;
            rhs.reference_symbol_jumps.clear();
            rhs.executable_offset = 0;

            return *this;
        }
        ~LiveGeneratorOutput();
        size_t num_reference_symbol_jumps() const;
        void set_reference_symbol_jump(size_t jump_index, recomp_func_t* func);
        ReferenceJumpDetails get_reference_symbol_jump_details(size_t jump_index);
        void populate_import_symbol_jumps(size_t import_index, recomp_func_t* func);
        bool good = false;
        // Storage for string literals referenced by recompiled code. These are allocated as unique_ptr arrays
        // to prevent them from moving, as the referenced address is baked into the recompiled code.
        std::vector<std::unique_ptr<char[]>> string_literals;
        // Storage for jump tables referenced by recompiled code (vector of arrays of pointers). These are also
        // allocated as unique_ptr arrays for the same reason as strings.
        std::vector<std::unique_ptr<void*[]>> jump_tables;
        // Recompiled code.
        void* code;
        // Size of the recompiled code.
        size_t code_size;
        // Pointers to each individual function within the recompiled code.
        std::vector<recomp_func_t*> functions;
    private:
        // List of jump details and the corresponding jump instruction address. These jumps get populated after recompilation is complete
        // during dependency resolution.
        std::vector<std::pair<ReferenceJumpDetails, void*>> reference_symbol_jumps;
        // Mapping of import symbol index to any jumps to that import symbol.
        std::unordered_multimap<size_t, void*> import_jumps_by_index;
        // sljit executable offset.
        int64_t executable_offset;

        friend class LiveGenerator;
    };
    struct LiveGeneratorInputs {
        uint32_t base_event_index;
        void (*cop0_status_write)(recomp_context* ctx, gpr value);
        gpr (*cop0_status_read)(recomp_context* ctx);
        void (*switch_error)(const char* func, uint32_t vram, uint32_t jtbl);
        void (*do_break)(uint32_t vram);
        recomp_func_t* (*get_function)(int32_t vram);
        void (*syscall_handler)(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram);
        void (*pause_self)(uint8_t* rdram);
        void (*trigger_event)(uint8_t* rdram, recomp_context* ctx, uint32_t event_index);
        int32_t *reference_section_addresses;
        int32_t *local_section_addresses;
        void (*run_hook)(uint8_t* rdram, recomp_context* ctx, size_t hook_table_index);
        // Maps function index in recompiler context to function's entry hook slot.
        std::unordered_map<size_t, size_t> entry_func_hooks;
        // Maps function index in recompiler context to function's return hook slot.
        std::unordered_map<size_t, size_t> return_func_hooks;
        // Maps section index in the generated code to original section index. Used by regenerated
        // code to relocate using the corresponding original section's address.
        std::vector<size_t> original_section_indices;
    };
    class LiveGenerator final : public Generator {
    public:
        LiveGenerator(size_t num_funcs, const LiveGeneratorInputs& inputs);
        ~LiveGenerator();
        // Prevent moving or copying.
        LiveGenerator(const LiveGenerator& rhs) = delete;
        LiveGenerator(LiveGenerator&& rhs) = delete;
        LiveGenerator& operator=(const LiveGenerator& rhs) = delete;
        LiveGenerator& operator=(LiveGenerator&& rhs) = delete;

        LiveGeneratorOutput finish();
        void process_binary_op(const BinaryOp& op, const InstructionContext& ctx) const final;
        void process_unary_op(const UnaryOp& op, const InstructionContext& ctx) const final;
        void process_store_op(const StoreOp& op, const InstructionContext& ctx) const final;
        void emit_function_start(const std::string& function_name, size_t func_index) const final;
        void emit_function_end() const final;
        void emit_function_call_lookup(uint32_t addr) const final;
        void emit_function_call_by_register(int reg) const final;
        void emit_function_call_reference_symbol(const Context& context, uint16_t section_index, size_t symbol_index, uint32_t target_section_offset) const final;
        void emit_function_call(const Context& context, size_t function_index) const final;
        void emit_named_function_call(const std::string& function_name) const final;
        void emit_goto(const std::string& target) const final;
        void emit_label(const std::string& label_name) const final;
        void emit_jtbl_addend_declaration(const JumpTable& jtbl, int reg) const final;
        void emit_branch_condition(const ConditionalBranchOp& op, const InstructionContext& ctx) const final;
        void emit_branch_close() const final;
        void emit_switch(const Context& recompiler_context, const JumpTable& jtbl, int reg) const final;
        void emit_case(int case_index, const std::string& target_label) const final;
        void emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const final;
        void emit_switch_close() const final;
        void emit_return(const Context& context, size_t func_index) const final;
        void emit_check_fr(int fpr) const final;
        void emit_check_nan(int fpr, bool is_double) const final;
        void emit_cop0_status_read(int reg) const final;
        void emit_cop0_status_write(int reg) const final;
        void emit_cop1_cs_read(int reg) const final;
        void emit_cop1_cs_write(int reg) const final;
        void emit_muldiv(InstrId instr_id, int reg1, int reg2) const final;
        void emit_syscall(uint32_t instr_vram) const final;
        void emit_do_break(uint32_t instr_vram) const final;
        void emit_trap(const TrapOp& op, const InstructionContext& ctx, uint32_t instr_vram) const final;
        void emit_pause_self() const final;
        void emit_trigger_event(uint32_t event_index) const final;
        void emit_comment(const std::string& comment) const final;
    private:
        void get_operand_string(Operand operand, UnaryOpType operation, const InstructionContext& context, std::string& operand_string) const;
        void get_binary_expr_string(BinaryOpType type, const BinaryOperands& operands, const InstructionContext& ctx, const std::string& output, std::string& expr_string) const;
        void get_notation(BinaryOpType op_type, std::string& func_string, std::string& infix_string) const;
        // Loads the relocated address specified by the instruction context into the target register.
        void load_relocated_address(const InstructionContext& ctx, int reg) const;
        sljit_compiler* compiler;
        LiveGeneratorInputs inputs;
        mutable std::unique_ptr<LiveGeneratorContext> context;
        mutable bool errored;
    };

    void live_recompiler_init();
    bool recompile_function_live(LiveGenerator& generator, const Context& context, size_t function_index, std::ostream& output_file, std::span<std::vector<uint32_t>> static_funcs_out, bool tag_reference_relocs);

    class ShimFunction {
    private:
        void* code;
        recomp_func_t* func;
    public:
        ShimFunction(recomp_func_ext_t* to_shim, uintptr_t value);
        ~ShimFunction();
        recomp_func_t* get_func() { return func; }
    };
}

#endif