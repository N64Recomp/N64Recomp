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
        uint32_t symbol_index; // Only used for reference symbols and special section symbols
        uint16_t target_section;
        RelocType type;
        bool reference_symbol;
    };

    // Special section indices.
    constexpr uint16_t SectionAbsolute = (uint16_t)-2;
    constexpr uint16_t SectionImport = (uint16_t)-3; // Imported symbols for mods
    constexpr uint16_t SectionEvent = (uint16_t)-4;

    // Special section names.
    constexpr std::string_view PatchSectionName = ".recomp_patch";
    constexpr std::string_view ForcedPatchSectionName = ".recomp_force_patch";
    constexpr std::string_view ExportSectionName = ".recomp_export";
    constexpr std::string_view EventSectionName = ".recomp_event";
    constexpr std::string_view ImportSectionPrefix = ".recomp_import.";
    constexpr std::string_view CallbackSectionPrefix = ".recomp_callback.";

    // Special mod names.
    constexpr std::string_view ModSelf = ".";
    constexpr std::string_view ModBaseRecomp = "*";

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
        std::string name;
        uint16_t section_index;
        uint32_t section_offset;
        bool is_function;
    };

    struct ElfParsingConfig {
        std::string bss_section_suffix;
        // Functions with manual size overrides
        std::unordered_map<std::string, size_t> manually_sized_funcs;
        // The section names that were specified as relocatable
        std::unordered_set<std::string> relocatable_sections;
        bool has_entrypoint;
        int32_t entrypoint_address;
        bool use_absolute_symbols;
        bool unpaired_lo16_warnings;
        bool all_sections_relocatable;
    };
    
    struct DataSymbol {
        uint32_t vram;
        std::string name;

        DataSymbol(uint32_t vram, std::string&& name) : vram(vram), name(std::move(name)) {}
    };

    using DataSymbolMap = std::unordered_map<uint16_t, std::vector<DataSymbol>>;

    extern const std::unordered_set<std::string> reimplemented_funcs;
    extern const std::unordered_set<std::string> ignored_funcs;
    extern const std::unordered_set<std::string> renamed_funcs;

    struct Dependency {
        uint8_t major_version;
        uint8_t minor_version;
        uint8_t patch_version;
        std::string mod_id;
    };

    struct ImportSymbol {
        ReferenceSymbol base;
        size_t dependency_index;
    };

    struct DependencyEvent {
        size_t dependency_index;
        std::string event_name;
    };

    struct EventSymbol {
        ReferenceSymbol base;
    };

    struct Callback {
        size_t function_index;
        size_t dependency_event_index;
    };

    struct SymbolReference {
        // Reference symbol section index, or one of the special section indices such as SectionImport.
        uint16_t section_index;
        size_t symbol_index;
    };

    enum class ReplacementFlags : uint32_t {
        Force = 1 << 0,
    };
    inline ReplacementFlags operator&(ReplacementFlags lhs, ReplacementFlags rhs) { return ReplacementFlags(uint32_t(lhs) & uint32_t(rhs)); }
    inline ReplacementFlags operator|(ReplacementFlags lhs, ReplacementFlags rhs) { return ReplacementFlags(uint32_t(lhs) | uint32_t(rhs)); }

    struct FunctionReplacement {
        uint32_t func_index;
        uint32_t original_section_vrom;
        uint32_t original_vram;
        ReplacementFlags flags;
    };

    class Context {
    private:
        //// Reference symbols (used for populating relocations for patches)
        // A list of the sections that contain the reference symbols.
        std::vector<ReferenceSection> reference_sections;
        // A list of the reference symbols.
        std::vector<ReferenceSymbol> reference_symbols;
        // Mapping of symbol name to reference symbol index.
        std::unordered_map<std::string, SymbolReference> reference_symbols_by_name;
    public:
        std::vector<Section> sections;
        std::vector<Function> functions;
        // A list of the list of each function (by index in `functions`) in a given section
        std::vector<std::vector<size_t>> section_functions;
        // A mapping of vram address to every function with that address.
        std::unordered_map<uint32_t, std::vector<size_t>> functions_by_vram;
        // A mapping of bss section index to the corresponding non-bss section index.
        std::unordered_map<uint16_t, uint16_t> bss_section_to_section;
        // The target ROM being recompiled, TODO move this outside of the context to avoid making a copy for mod contexts.
        // Used for reading relocations and for the output binary feature.
        std::vector<uint8_t> rom;

        //// Only used by the CLI, TODO move this to a struct in the internal headers.
        // A mapping of function name to index in the functions vector
        std::unordered_map<std::string, size_t> functions_by_name;

        //// Mod dependencies and their symbols
        
        //// Imported values
        // List of dependencies.
        std::vector<Dependency> dependencies;
        // Mapping of dependency name to dependency index.
        std::unordered_map<std::string, size_t> dependencies_by_name;
        // List of symbols imported from dependencies.
        std::vector<ImportSymbol> import_symbols;
        // List of events imported from dependencies.
        std::vector<DependencyEvent> dependency_events;
        // Mappings of dependency event name to the index in dependency_events, all indexed by dependency.
        std::vector<std::unordered_map<std::string, size_t>> dependency_events_by_name;
        // Mappings of dependency import name to index in import_symbols, all indexed by dependency.
        std::vector<std::unordered_map<std::string, size_t>> dependency_imports_by_name;

        //// Exported values
        // List of function replacements, which contains the original function to replace and the function index to replace it with.
        std::vector<FunctionReplacement> replacements;
        // Indices of every exported function.
        std::vector<size_t> exported_funcs;
        // List of callbacks, which contains the function for the callback and the dependency event it attaches to.
        std::vector<Callback> callbacks;
        // List of symbols from events, which contains the names of events that this context provides.
        std::vector<EventSymbol> event_symbols;

        // Imports sections and function symbols from a provided context into this context's reference sections and reference functions.
        bool import_reference_context(const Context& reference_context);
        // Reads a data symbol file and adds its contents into this context's reference data symbols.
        bool read_data_reference_syms(const std::filesystem::path& data_syms_file_path);

        static bool from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, Context& out, bool with_relocs);
        static bool from_elf_file(const std::filesystem::path& elf_file_path, Context& out, const ElfParsingConfig& flags, bool for_dumping_context, DataSymbolMap& data_syms_out, bool& found_entrypoint_out);

        Context() = default;

        bool add_dependency(const std::string& id, uint8_t major_version, uint8_t minor_version, uint8_t patch_version) {
            if (dependencies_by_name.contains(id)) {
                return false;
            }

            size_t dependency_index = dependencies.size();
            dependencies.emplace_back(N64Recomp::Dependency {
                .major_version = major_version,
                .minor_version = minor_version,
                .patch_version = patch_version,
                .mod_id = id
            });

            dependencies_by_name.emplace(id, dependency_index);
            dependency_events_by_name.resize(dependencies.size());
            dependency_imports_by_name.resize(dependencies.size());

            return true;
        }

        bool add_dependencies(const std::vector<Dependency>& new_dependencies) {
            dependencies.reserve(dependencies.size() + new_dependencies.size());
            dependencies_by_name.reserve(dependencies_by_name.size() + new_dependencies.size());

            // Check if any of the dependencies already exist and fail if so.
            for (const Dependency& dep : new_dependencies) {
                if (dependencies_by_name.contains(dep.mod_id)) {
                    return false;
                }
            }

            for (const Dependency& dep : new_dependencies) {
                size_t dependency_index = dependencies.size();
                dependencies.emplace_back(dep);
                dependencies_by_name.emplace(dep.mod_id, dependency_index);
            }

            dependency_events_by_name.resize(dependencies.size());
            dependency_imports_by_name.resize(dependencies.size());
            return true;
        }

        bool find_dependency(const std::string& mod_id, size_t& dependency_index) {
            auto find_it = dependencies_by_name.find(mod_id);
            if (find_it == dependencies_by_name.end()) {
                return false;
            }
            dependency_index = find_it->second;
            return true;
        }

        size_t find_function_by_vram_section(uint32_t vram, size_t section_index) const {
            auto find_it = functions_by_vram.find(vram);
            if (find_it == functions_by_vram.end()) {
                return (size_t)-1;
            }

            for (size_t function_index : find_it->second) {
                if (functions[function_index].section_index == section_index) {
                    return function_index;
                }
            }

            return (size_t)-1;
        }

        bool has_reference_symbols() const {
            return !reference_symbols.empty() || !import_symbols.empty() || !event_symbols.empty();
        }

        bool is_regular_reference_section(uint16_t section_index) const {
            return section_index != SectionImport && section_index != SectionEvent;
        }

        bool find_reference_symbol(const std::string& symbol_name, SymbolReference& ref_out) const {
            auto find_sym_it = reference_symbols_by_name.find(symbol_name);

            // Check if the symbol was found.
            if (find_sym_it == reference_symbols_by_name.end()) {
                return false;
            }

            ref_out = find_sym_it->second;
            return true;
        }

        bool reference_symbol_exists(const std::string& symbol_name) const {
            SymbolReference dummy_ref;
            return find_reference_symbol(symbol_name, dummy_ref);
        }

        bool find_regular_reference_symbol(const std::string& symbol_name, SymbolReference& ref_out) const {
            SymbolReference ref_found;
            if (!find_reference_symbol(symbol_name, ref_found)) {
                return false;
            }

            // Ignore reference symbols in special sections.
            if (!is_regular_reference_section(ref_found.section_index)) {
                return false;
            }

            ref_out = ref_found;
            return true;
        }

        const ReferenceSymbol& get_reference_symbol(uint16_t section_index, size_t symbol_index) const {
            if (section_index == SectionImport) {
                return import_symbols[symbol_index].base;
            }
            else if (section_index == SectionEvent) {
                return event_symbols[symbol_index].base;
            }
            return reference_symbols[symbol_index];
        }

        size_t num_regular_reference_symbols() {
            return reference_symbols.size();
        }

        const ReferenceSymbol& get_regular_reference_symbol(size_t index) const {
            return reference_symbols[index];
        }

        const ReferenceSymbol& get_reference_symbol(const SymbolReference& ref) const {
            return get_reference_symbol(ref.section_index, ref.symbol_index);
        }

        bool is_reference_section_relocatable(uint16_t section_index) const {
            if (section_index == SectionAbsolute) {
                return false;
            }
            else if (section_index == SectionImport || section_index == SectionEvent) {
                return true;
            }
            return reference_sections[section_index].relocatable;
        }

        bool add_reference_symbol(const std::string& symbol_name, uint16_t section_index, uint32_t vram, bool is_function) {
            uint32_t section_vram;

            if (section_index == SectionAbsolute) {
                section_vram = 0;
            }
            else if (section_index < reference_sections.size()) {
                section_vram = reference_sections[section_index].ram_addr;
            }
            // Invalid section index.
            else {
                return false;
            }

            // TODO Check if reference_symbols_by_name already contains the name and show a conflict error if so.
            reference_symbols_by_name.emplace(symbol_name, N64Recomp::SymbolReference{
                .section_index = section_index,
                .symbol_index = reference_symbols.size()
            });

            reference_symbols.emplace_back(N64Recomp::ReferenceSymbol{
                .name = symbol_name,
                .section_index = section_index,
                .section_offset = vram - section_vram,
                .is_function = is_function
            });
            return true;
        }

        void add_import_symbol(const std::string& symbol_name, size_t dependency_index) {
            // TODO Check if dependency_imports_by_name[dependency_index] already contains the name and show a conflict error if so.
            dependency_imports_by_name[dependency_index][symbol_name] = import_symbols.size();
            import_symbols.emplace_back(
                N64Recomp::ImportSymbol {
                    .base = N64Recomp::ReferenceSymbol {
                        .name = symbol_name,
                        .section_index = N64Recomp::SectionImport,
                        .section_offset = 0,
                        .is_function = true
                    },
                    .dependency_index = dependency_index,
                }
            );
        }

        bool find_import_symbol(const std::string& symbol_name, size_t dependency_index, SymbolReference& ref_out) const {
            if (dependency_index >= dependencies.size()) {
                return false;
            }

            auto find_it = dependency_imports_by_name[dependency_index].find(symbol_name);
            if (find_it == dependency_imports_by_name[dependency_index].end()) {
                return false;
            }

            ref_out.section_index = SectionImport;
            ref_out.symbol_index = find_it->second;
            return true;
        }

        void add_event_symbol(const std::string& symbol_name) {
            // TODO Check if reference_symbols_by_name already contains the name and show a conflict error if so.
            reference_symbols_by_name[symbol_name] = N64Recomp::SymbolReference {
                .section_index = N64Recomp::SectionEvent,
                .symbol_index = event_symbols.size()
            };
            event_symbols.emplace_back(
                N64Recomp::EventSymbol {
                    .base = N64Recomp::ReferenceSymbol {
                        .name = symbol_name,
                        .section_index = N64Recomp::SectionEvent,
                        .section_offset = 0,
                        .is_function = true
                    }
                }
            );
        }

        bool find_event_symbol(const std::string& symbol_name, SymbolReference& ref_out) const {
            SymbolReference ref_found;
            if (!find_reference_symbol(symbol_name, ref_found)) {
                return false;
            }

            // Ignore reference symbols that aren't in the event section.
            if (ref_found.section_index != SectionEvent) {
                return false;
            }

            ref_out = ref_found;
            return true;
        }

        bool add_dependency_event(const std::string& event_name, size_t dependency_index, size_t& dependency_event_index) {
            if (dependency_index >= dependencies.size()) {
                return false;
            }

            // Prevent adding the same event to a dependency twice. This isn't an error, since a mod could register
            // multiple callbacks to the same event.
            auto find_it = dependency_events_by_name[dependency_index].find(event_name);
            if (find_it != dependency_events_by_name[dependency_index].end()) {
                dependency_event_index = find_it->second;
                return true;
            }

            dependency_event_index = dependency_events.size();
            dependency_events.emplace_back(DependencyEvent{
                .dependency_index = dependency_index,
                .event_name = event_name
            });
            dependency_events_by_name[dependency_index][event_name] = dependency_event_index;
            return true;
        }

        bool add_callback(size_t dependency_event_index, size_t function_index) {
            callbacks.emplace_back(Callback{
                .function_index = function_index,
                .dependency_event_index = dependency_event_index
            });
            return true;
        }

        uint32_t get_reference_section_vram(uint16_t section_index) const {
            if (section_index == N64Recomp::SectionAbsolute) {
                return 0;
            }
            else if (!is_regular_reference_section(section_index)) {
                return 0;
            }
            else {
                return reference_sections[section_index].ram_addr;
            }
        }

        uint32_t get_reference_section_rom(uint16_t section_index) const {
            if (section_index == N64Recomp::SectionAbsolute) {
                return (uint32_t)-1;
            }
            else if (!is_regular_reference_section(section_index)) {
                return (uint32_t)-1;
            }
            else {
                return reference_sections[section_index].rom_addr;
            }
        }

        void copy_reference_sections_from(const Context& rhs) {
            reference_sections = rhs.reference_sections;
        }
    };

    bool recompile_function(const Context& context, const Function& func, std::ofstream& output_file, std::span<std::vector<uint32_t>> static_funcs, bool tag_reference_relocs);

    enum class ModSymbolsError {
        Good,
        NotASymbolFile,
        UnknownSymbolFileVersion,
        CorruptSymbolFile,
        FunctionOutOfBounds,
    };

    ModSymbolsError parse_mod_symbols(std::span<const char> data, std::span<const uint8_t> binary, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, const Context& reference_context, Context& context_out);
    std::vector<uint8_t> symbols_to_bin_v1(const Context& mod_context);

    inline bool validate_mod_name(std::string_view str) {
        // Disallow mod names with a colon in them, since you can't specify that in a dependency string orin callbacks.
        for (char c : str) {
            if (c == ':') {
                return false;
            }
        }
        return true;
    }

    inline bool validate_mod_name(const std::string& str) {
        return validate_mod_name(std::string_view{str});
    }
}

#endif
