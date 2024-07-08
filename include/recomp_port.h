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
#include "rabbitizer.hpp"

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
        uint32_t section_offset;
        uint32_t symbol_index;
        uint32_t target_section;
        RelocType type;
        bool reference_symbol;
    };

    constexpr uint16_t SectionSelf = (uint16_t)-1;
    constexpr uint16_t SectionAbsolute = (uint16_t)-2;
    struct Section {
        uint32_t rom_addr = 0;
        uint32_t ram_addr = 0;
        uint32_t size = 0;
        std::vector<uint32_t> function_addrs;
        std::vector<Reloc> relocs;
        std::string name;
        uint16_t bss_section_index = (uint16_t)-1;
        bool executable = false;
        bool relocatable = false;
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
        // A mapping of function name to index in the functions vector
        std::unordered_map<std::string, size_t> functions_by_name;
        std::vector<uint8_t> rom;
        // A list of the list of each function (by index in `functions`) in a given section
        std::vector<std::vector<size_t>> section_functions;
        // The section names that were specified as relocatable
        std::unordered_set<std::string> relocatable_sections;
        // Functions with manual size overrides
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
        int executable_section_count;

        // Imports sections and function symbols from a provided context into this context's reference sections and reference functions.
        void import_reference_context(const Context& reference_context);
        // Reads a data symbol file and adds its contents into this context's reference data symbols.
        bool read_data_reference_syms(const std::filesystem::path& data_syms_file_path);

        static bool from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, Context& out, bool with_relocs);

        Context() = default;
    };

    bool recompile_function(const Context& context, const Function& func, const std::string& recomp_include, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs, bool write_header);
}

#endif
