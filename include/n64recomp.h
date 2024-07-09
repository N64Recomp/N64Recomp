#ifndef __RECOMP_PORT__
#define __RECOMP_PORT__

#include <span>
#include <string_view>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

namespace N64Recomp {
    struct Function {
        uint32_t vram;
        uint32_t rom;
        std::vector<uint32_t> words;
        std::string name;
        uint16_t section_index;
        bool ignored;
        bool reimplemented;
        bool stubbed;
        std::unordered_map<int32_t, std::string> function_hooks;

        Function(uint32_t vram, uint32_t rom, std::vector<uint32_t> words, std::string name, uint16_t section_index, bool ignored = false, bool reimplemented = false, bool stubbed = false)
                : vram(vram), rom(rom), words(std::move(words)), name(std::move(name)), section_index(section_index), ignored(ignored), reimplemented(reimplemented), stubbed(stubbed) {}
        Function() = default;
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
        uint32_t target_section_offset;
        uint32_t symbol_index; // Only used for reference symbols
        uint16_t target_section;
        RelocType type;
        bool reference_symbol;
    };

    constexpr uint16_t SectionSelf = (uint16_t)-1;
    constexpr uint16_t SectionAbsolute = (uint16_t)-2;
    struct Section {
        uint32_t rom_addr = 0;
        uint32_t ram_addr = 0;
        uint32_t size = 0;
        uint32_t bss_size = 0; // not populated when using a symbol toml
        std::vector<uint32_t> function_addrs; // only used by the CLI (to find the size of static functions)
        std::vector<Reloc> relocs;
        std::string name;
        uint16_t bss_section_index = (uint16_t)-1;
        bool executable = false;
        bool relocatable = false; // TODO is this needed? relocs being non-empty should be an equivalent check.
        bool has_mips32_relocs = false;
    };

    struct ReferenceSection {
        uint32_t rom_addr;
        uint32_t ram_addr;
        uint32_t size;
        bool relocatable;
    };

    struct ReferenceSymbol {
        uint16_t section_index;
        uint32_t section_offset;
        bool is_function;
    };

    struct Context {
        // ROM address of each section
        std::vector<Section> sections;
        std::vector<Function> functions;
        std::unordered_map<uint32_t, std::vector<size_t>> functions_by_vram;
        // The target ROM being recompiled, TODO move this outside of the context to avoid making a copy for mod contexts.
        // Used for reading relocations and for the output binary feature.
        std::vector<uint8_t> rom;

        //// Only used by the CLI, TODO move these to a struct in the internal headers.
        // A mapping of function name to index in the functions vector
        std::unordered_map<std::string, size_t> functions_by_name; 
        // A list of the list of each function (by index in `functions`) in a given section
        std::vector<std::vector<size_t>> section_functions;
        // The section names that were specified as relocatable (only used for elf files)
        std::unordered_set<std::string> relocatable_sections; // 
        // Functions with manual size overrides (only used for elf files)
        std::unordered_map<std::string, size_t> manually_sized_funcs;

        //// Reference symbols (used for populating relocations for patches)
        // A list of the sections that contain the reference symbols.
        std::vector<ReferenceSection> reference_sections;
        // A list of the reference symbols.
        std::vector<ReferenceSymbol> reference_symbols;
        // Name of every reference symbol in the same order as `reference_symbols`.
        std::vector<std::string> reference_symbol_names;
        // Mapping of symbol name to reference symbol index.
        std::unordered_map<std::string, size_t> reference_symbols_by_name;

        // Imports sections and function symbols from a provided context into this context's reference sections and reference functions.
        void import_reference_context(const Context& reference_context);
        // Reads a data symbol file and adds its contents into this context's reference data symbols.
        bool read_data_reference_syms(const std::filesystem::path& data_syms_file_path);

        static bool from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, Context& out, bool with_relocs);

        Context() = default;
    };

    bool recompile_function(const Context& context, const Function& func, const std::string& recomp_include, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs, bool write_header);
    
    enum class ReplacementFlags : uint32_t {
        Force = 1 << 0,
    };
    
    struct FunctionReplacement {
        uint32_t func_index;
        uint32_t original_vram;
        ReplacementFlags flags;
    };

    struct ModSectionInfo {
        uint32_t original_rom_addr;
        std::vector<FunctionReplacement> replacements;

    };

    struct ModContext {
        Context base_context;
        std::vector<ModSectionInfo> section_info;
    };
    enum class ModSymbolsError {
        Good,
        NotASymbolFile,
        UnknownSymbolFileVersion,
        CorruptSymbolFile,
        FunctionOutOfBounds,
    };

    ModSymbolsError parse_mod_symbols(std::span<const char> data, std::span<const uint8_t> binary, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, Context& context_out, ModContext& mod_context_out);
}

#endif