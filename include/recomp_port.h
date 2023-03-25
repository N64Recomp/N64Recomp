#ifndef __RECOMP_PORT__
#define __RECOMP_PORT__

#include <span>
#include <string_view>
#include <cstdint>
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

    struct Config {
        int32_t entrypoint;
        std::filesystem::path elf_path;
        std::filesystem::path output_func_path;
        std::filesystem::path relocatable_sections_path;
        std::vector<std::string> stubbed_funcs;
        DeclaredFunctionMap declared_funcs;
        std::vector<InstructionPatch> instruction_patches;

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
    };

    struct AbsoluteJump {
        uint32_t jump_target;
        uint32_t instruction_vram;
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
        bool needs_relocation;
    };

    struct Section {
        ELFIO::Elf_Xword rom_addr = 0;
        ELFIO::Elf64_Addr ram_addr = 0;
        ELFIO::Elf_Xword size = 0;
        std::vector<uint32_t> function_addrs;
        std::vector<Reloc> relocs;
        std::string name;
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
    };

    bool analyze_function(const Context& context, const Function& function, const std::vector<rabbitizer::InstructionCpu>& instructions, FunctionStats& stats);
    bool recompile_function(const Context& context, const Function& func, const std::filesystem::path& output_path, std::span<std::vector<uint32_t>> static_funcs);
}

#endif
