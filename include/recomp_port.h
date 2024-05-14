#ifndef __RECOMP_PORT__
#define __RECOMP_PORT__

#include <span>
#include <string_view>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>
#include <span>
#include <unordered_set>
#include <filesystem>
#include "rabbitizer.hpp"
#include "elfio/elfio.hpp"

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

namespace RecompPort {

    // Potential argument types for function declarations
    enum class FunctionArgType {
        u32,
        s32,
    };

    // Mapping of function name to argument types
    using DeclaredFunctionMap = std::unordered_map<std::string, std::vector<FunctionArgType>>;

    struct InstructionPatch {
        std::string func_name;
        int32_t vram;
        uint32_t value;
    };

    struct FunctionSize {
        std::string func_name;
        uint32_t size_bytes;

        FunctionSize(const std::string& func_name, uint32_t size_bytes) : func_name(func_name), size_bytes(size_bytes) {}
    };

    struct ManualFunction {
        std::string func_name;
        std::string section_name;
        uint32_t vram;
        uint32_t size;

        ManualFunction(const std::string& func_name, std::string section_name, uint32_t vram, uint32_t size) : func_name(func_name), section_name(std::move(section_name)), vram(vram), size(size) {}
    };

    struct Config {
        int32_t entrypoint;
        bool has_entrypoint;
        bool uses_mips3_float_mode;
        bool single_file_output;
        bool use_absolute_symbols;
        std::filesystem::path elf_path;
        std::filesystem::path symbols_file_path;
        std::filesystem::path rom_file_path;
        std::filesystem::path output_func_path;
        std::filesystem::path relocatable_sections_path;
        std::vector<std::string> stubbed_funcs;
        std::vector<std::string> ignored_funcs;
        DeclaredFunctionMap declared_funcs;
        std::vector<InstructionPatch> instruction_patches;
        std::vector<FunctionSize> manual_func_sizes;
        std::vector<ManualFunction> manual_functions;
        std::string bss_section_suffix;

        Config(const char* path);
        bool good() { return !bad; }
    private:
        bool bad;
    };

    struct JumpTable {
        uint32_t vram;
        uint32_t addend_reg;
        uint32_t rom;
        uint32_t lw_vram;
        uint32_t addu_vram;
        uint32_t jr_vram;
        std::vector<uint32_t> entries;

        JumpTable(uint32_t vram, uint32_t addend_reg, uint32_t rom, uint32_t lw_vram, uint32_t addu_vram, uint32_t jr_vram, std::vector<uint32_t>&& entries)
                : vram(vram), addend_reg(addend_reg), rom(rom), lw_vram(lw_vram), addu_vram(addu_vram), jr_vram(jr_vram), entries(std::move(entries)) {}
    };

    struct AbsoluteJump {
        uint32_t jump_target;
        uint32_t instruction_vram;

        AbsoluteJump(uint32_t jump_target, uint32_t instruction_vram) : jump_target(jump_target), instruction_vram(instruction_vram) {}
    };

    struct Function {
        uint32_t vram;
        uint32_t rom;
        std::vector<uint32_t> words;
        std::string name;
        ELFIO::Elf_Half section_index;
        bool ignored;
        bool reimplemented;
        bool stubbed;

        Function(uint32_t vram, uint32_t rom, std::vector<uint32_t> words, std::string name, ELFIO::Elf_Half section_index, bool ignored = false, bool reimplemented = false, bool stubbed = false)
                : vram(vram), rom(rom), words(std::move(words)), name(std::move(name)), section_index(section_index), ignored(ignored), reimplemented(reimplemented), stubbed(stubbed) {}
    };

    enum class RelocType : uint8_t {
        R_MIPS_NONE = 0,
        R_MIPS_16,
        R_MIPS_32,
        R_MIPS_REL32,
        R_MIPS_26,
        R_MIPS_HI16,
        R_MIPS_LO16,
        R_MIPS_GPREL16,
    };

    struct Reloc {
        uint32_t address;
        uint32_t target_address;
        uint32_t symbol_index;
        uint32_t target_section;
        RelocType type;
    };

    struct Section {
        ELFIO::Elf_Xword rom_addr = 0;
        ELFIO::Elf64_Addr ram_addr = 0;
        ELFIO::Elf_Xword size = 0;
        std::vector<uint32_t> function_addrs;
        std::vector<Reloc> relocs;
        std::string name;
        ELFIO::Elf_Half bss_section_index = (ELFIO::Elf_Half)-1;
        bool executable = false;
        bool relocatable = false;
    };

    struct FunctionStats {
        std::vector<JumpTable> jump_tables;
        std::vector<AbsoluteJump> absolute_jumps;
    };

    struct Context {
        // ROM address of each section
        std::vector<Section> sections;
        std::vector<Function> functions;
        std::unordered_map<uint32_t, std::vector<size_t>> functions_by_vram;
        // A mapping of function name to index in the functions vector
        std::unordered_map<std::string, size_t> functions_by_name;
        std::vector<uint8_t> rom;
        // A list of the list of each function (by index in `functions`) in a given section
        std::vector<std::vector<size_t>> section_functions;
        // The section names that were specified as relocatable
        std::unordered_set<std::string> relocatable_sections;
        // Functions with manual size overrides
        std::unordered_map<std::string, size_t> manually_sized_funcs;
        int executable_section_count;

        Context(const ELFIO::elfio& elf_file) {
            sections.resize(elf_file.sections.size());
            section_functions.resize(elf_file.sections.size());
            functions.reserve(1024);
            functions_by_vram.reserve(functions.capacity());
            functions_by_name.reserve(functions.capacity());
            rom.reserve(8 * 1024 * 1024);
            executable_section_count = 0;
        }

        static bool from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, Context& out);

        Context() = default;
    };

    bool analyze_function(const Context& context, const Function& function, const std::vector<rabbitizer::InstructionCpu>& instructions, FunctionStats& stats);
    bool recompile_function(const Context& context, const Config& config, const Function& func, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs, bool write_header);
}

#endif
