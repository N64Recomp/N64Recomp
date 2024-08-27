#include <cstring>

#include "n64recomp.h"

struct FileHeader {
    char magic[8]; // N64RSYMS
    uint32_t version;
};

struct FileSubHeaderV1 {
    uint32_t num_sections;
    uint32_t num_dependencies;
    uint32_t num_imports;
    uint32_t num_dependency_events;
    uint32_t num_replacements;
    uint32_t num_exports;
    uint32_t num_callbacks;
    uint32_t num_provided_events;
    uint32_t string_data_size;
};

struct SectionHeaderV1 {
    uint32_t flags;
    uint32_t file_offset;
    uint32_t vram;
    uint32_t rom_size;
    uint32_t bss_size;
    uint32_t num_funcs;
    uint32_t num_relocs;
};

struct FuncV1 {
    uint32_t section_offset;
    uint32_t size;
};

// Local section flag, if set then the reloc is pointing to a section within the mod and the vrom is the section index.
constexpr uint32_t SectionSelfVromFlagV1 = 0x80000000;

// Special sections
constexpr uint32_t SectionImportVromV1 = 0xFFFFFFFE;
constexpr uint32_t SectionEventVromV1 = 0xFFFFFFFD;

struct RelocV1 {
    uint32_t section_offset;
    uint32_t type;
    uint32_t target_section_offset_or_index; // If this reloc references a special section (see above), this indicates the section's symbol index instead
    uint32_t target_section_vrom;
};

struct DependencyV1 {
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t patch_version;
    uint8_t reserved;
    uint32_t mod_id_start;
    uint32_t mod_id_size;
};

struct ImportV1 {
    uint32_t name_start;
    uint32_t name_size;
    uint32_t dependency;
};

struct DependencyEventV1 {
    uint32_t name_start;
    uint32_t name_size;
    uint32_t dependency;
};

struct ReplacementV1 {
    uint32_t func_index;
    uint32_t original_section_vrom;
    uint32_t original_vram;
    uint32_t flags; // force
};

struct ExportV1 {
    uint32_t func_index;
    uint32_t name_start; // offset into the string data
    uint32_t name_size;
};

struct CallbackV1 {
    uint32_t dependency_event_index;
    uint32_t function_index;
};

struct EventV1 {
    uint32_t name_start;
    uint32_t name_size;
};

template <typename T>
const T* reinterpret_data(std::span<const char> data, size_t& offset, size_t count = 1) {
    if (offset + (sizeof(T) * count) > data.size()) {
        return nullptr;
    }

    size_t original_offset = offset;
    offset += sizeof(T) * count;
    return reinterpret_cast<const T*>(data.data() + original_offset);
}

bool check_magic(const FileHeader* header) {
    static const char good_magic[] = {'N','6','4','R','S','Y','M','S'};
    static_assert(sizeof(good_magic) == sizeof(FileHeader::magic));

    return memcmp(header->magic, good_magic, sizeof(good_magic)) == 0;
}

static inline uint32_t round_up_4(uint32_t value) {
    return (value + 3) & (~3);
}

bool parse_v1(std::span<const char> data, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, N64Recomp::Context& mod_context) {
    size_t offset = sizeof(FileHeader);
    const FileSubHeaderV1* subheader = reinterpret_data<FileSubHeaderV1>(data, offset);
    if (subheader == nullptr) {
        return false;
    }

    size_t num_sections = subheader->num_sections;
    size_t num_dependencies = subheader->num_dependencies;
    size_t num_imports = subheader->num_imports;
    size_t num_dependency_events = subheader->num_dependency_events;
    size_t num_replacements = subheader->num_replacements;
    size_t num_exports = subheader->num_exports;
    size_t num_callbacks = subheader->num_callbacks;
    size_t num_provided_events = subheader->num_provided_events;
    size_t string_data_size = subheader->string_data_size;

    if (string_data_size & 0b11) {
        printf("String data size of %zu is not a multiple of 4\n", string_data_size);
        return false;
    }

    const char* string_data = reinterpret_data<char>(data, offset, string_data_size);
    if (string_data == nullptr) {
        return false;
    }

    // TODO add proper creation methods for the remaining vectors and change these to reserves instead.
    mod_context.sections.resize(num_sections); // Add method
    mod_context.dependencies.reserve(num_dependencies);
    mod_context.dependencies_by_name.reserve(num_dependencies); 
    mod_context.import_symbols.reserve(num_imports);
    mod_context.dependency_events.reserve(num_dependency_events);
    mod_context.replacements.resize(num_replacements); // Add method
    mod_context.exported_funcs.resize(num_exports); // Add method
    mod_context.callbacks.reserve(num_callbacks);
    mod_context.event_symbols.reserve(num_provided_events);

    for (size_t section_index = 0; section_index < num_sections; section_index++) {
        const SectionHeaderV1* section_header = reinterpret_data<SectionHeaderV1>(data, offset);
        if (section_header == nullptr) {
            return false;
        }

        N64Recomp::Section& cur_section = mod_context.sections[section_index];

        cur_section.rom_addr = section_header->file_offset;
        cur_section.ram_addr = section_header->vram;
        cur_section.size = section_header->rom_size;
        cur_section.bss_size = section_header->bss_size;
        cur_section.name = "mod_section_" + std::to_string(section_index);
        cur_section.relocatable = true;
        uint32_t num_funcs = section_header->num_funcs;
        uint32_t num_relocs = section_header->num_relocs;


        const FuncV1* funcs = reinterpret_data<FuncV1>(data, offset, num_funcs);
        if (funcs == nullptr) {
            printf("Failed to read funcs (count: %d)\n", num_funcs);
            return false;
        }

        const RelocV1* relocs = reinterpret_data<RelocV1>(data, offset, num_relocs);
        if (relocs == nullptr) {
            printf("Failed to read relocs (count: %d)\n", num_relocs);
            return false;
        }

        size_t start_func_index = mod_context.functions.size();
        mod_context.functions.resize(mod_context.functions.size() + num_funcs);
        cur_section.relocs.resize(num_relocs);

        for (size_t func_index = 0; func_index < num_funcs; func_index++) {
            uint32_t func_rom_addr = cur_section.rom_addr + funcs[func_index].section_offset;
            if ((func_rom_addr & 0b11) != 0) {
                printf("Function %zu in section %zu file offset is not a multiple of 4\n", func_index, section_index);
                return false;
            }

            if ((funcs[func_index].size & 0b11) != 0) {
                printf("Function %zu in section %zu size is not a multiple of 4\n", func_index, section_index);
                return false;
            }

            N64Recomp::Function& cur_func = mod_context.functions[start_func_index + func_index];
            cur_func.vram = cur_section.ram_addr + funcs[func_index].section_offset;
            cur_func.rom = cur_section.rom_addr + funcs[func_index].section_offset;
            cur_func.words.resize(funcs[func_index].size / sizeof(uint32_t)); // Filled in later
            cur_func.section_index = section_index;
        }

        for (size_t reloc_index = 0; reloc_index < num_relocs; reloc_index++) {
            N64Recomp::Reloc& cur_reloc = cur_section.relocs[reloc_index];
            const RelocV1& reloc_in = relocs[reloc_index];
            cur_reloc.address = cur_section.ram_addr + reloc_in.section_offset;
            cur_reloc.type = static_cast<N64Recomp::RelocType>(reloc_in.type);
            uint32_t target_section_vrom = reloc_in.target_section_vrom;
            uint16_t reloc_target_section;
            uint32_t reloc_target_section_offset;
            uint32_t reloc_symbol_index;
            if (target_section_vrom == SectionImportVromV1) {
                reloc_target_section = N64Recomp::SectionImport;
                reloc_target_section_offset = 0; // Not used for imports or reference symbols.
                reloc_symbol_index = reloc_in.target_section_offset_or_index;
                cur_reloc.reference_symbol = true;
            }
            else if (target_section_vrom == SectionEventVromV1) {
                reloc_target_section = N64Recomp::SectionEvent;
                reloc_target_section_offset = 0; // Not used for event symbols.
                reloc_symbol_index = reloc_in.target_section_offset_or_index;
                cur_reloc.reference_symbol = true;
            }
            else if (target_section_vrom & SectionSelfVromFlagV1) {
                reloc_target_section = static_cast<uint16_t>(target_section_vrom & ~SectionSelfVromFlagV1);
                reloc_target_section_offset = reloc_in.target_section_offset_or_index;
                reloc_symbol_index = 0; // Not used for normal relocs.
                cur_reloc.reference_symbol = false;
                if (reloc_target_section >= mod_context.sections.size()) {
                    printf("Reloc %zu in section %zu references local section %u, but only %zu exist\n",
                        reloc_index, section_index, reloc_target_section, mod_context.sections.size());
                }
            }
            else {
                // TODO lookup by section index by original vrom
                auto find_section_it = sections_by_vrom.find(target_section_vrom);
                if (find_section_it == sections_by_vrom.end()) {
                    printf("Reloc %zu in section %zu has a target section vrom (%08X) that doesn't match any original section\n",
                        reloc_index, section_index, target_section_vrom);
                    return false;
                }
                reloc_target_section = find_section_it->second;
                reloc_target_section_offset = reloc_in.target_section_offset_or_index;
                reloc_symbol_index = 0; // Not used for normal relocs.
                cur_reloc.reference_symbol = true;
            }
            cur_reloc.target_section = reloc_target_section;
            cur_reloc.target_section_offset = reloc_target_section_offset;
            cur_reloc.symbol_index = reloc_symbol_index;
        }
    }

    const DependencyV1* dependencies = reinterpret_data<DependencyV1>(data, offset, num_dependencies);
    if (dependencies == nullptr) {
        printf("Failed to read dependencies (count: %zu)\n", num_dependencies);
        return false;
    }

    for (size_t dependency_index = 0; dependency_index < num_dependencies; dependency_index++) {
        const DependencyV1& dependency_in = dependencies[dependency_index];
        uint32_t mod_id_start = dependency_in.mod_id_start;
        uint32_t mod_id_size = dependency_in.mod_id_size;

        if (mod_id_start + mod_id_size > string_data_size) {
            printf("Dependency %zu has a name start of %u and size of %u, which extend beyond the string data's total size of %zu\n",
                dependency_index, mod_id_start, mod_id_size, string_data_size);
        }

        std::string_view mod_id{ string_data + mod_id_start, string_data + mod_id_start + mod_id_size };
        mod_context.add_dependency(std::string{mod_id}, dependency_in.major_version, dependency_in.minor_version, dependency_in.patch_version);
    }

    const ImportV1* imports = reinterpret_data<ImportV1>(data, offset, num_imports);
    if (imports == nullptr) {
        printf("Failed to read imports (count: %zu)\n", num_imports);
        return false;
    }

    for (size_t import_index = 0; import_index < num_imports; import_index++) {
        const ImportV1& import_in = imports[import_index];
        uint32_t name_start = import_in.name_start;
        uint32_t name_size = import_in.name_size;
        uint32_t dependency_index = import_in.dependency;

        if (name_start + name_size > string_data_size) {
            printf("Import %zu has a name start of %u and size of %u, which extend beyond the string data's total size of %zu\n",
                import_index, name_start, name_size, string_data_size);
        }

        if (dependency_index >= num_dependencies) {
            printf("Import %zu belongs to dependency %u, but only %zu dependencies were specified\n",
                import_index, dependency_index, num_dependencies);
        }

        std::string_view import_name{ string_data + name_start, string_data + name_start + name_size };

        mod_context.add_import_symbol(std::string{import_name}, dependency_index);
    }

    const DependencyEventV1* dependency_events = reinterpret_data<DependencyEventV1>(data, offset, num_dependency_events);
    if (dependency_events == nullptr) {
        printf("Failed to read dependency events (count: %zu)\n", num_dependency_events);
        return false;
    }
    
    for (size_t dependency_event_index = 0; dependency_event_index < num_dependency_events; dependency_event_index++) {
        const DependencyEventV1& dependency_event_in = dependency_events[dependency_event_index];
        uint32_t name_start = dependency_event_in.name_start;
        uint32_t name_size = dependency_event_in.name_size;
        uint32_t dependency_index = dependency_event_in.dependency;

        if (name_start + name_size > string_data_size) {
            printf("Dependency event %zu has a name start of %u and size of %u, which extend beyond the string data's total size of %zu\n",
                dependency_event_index, name_start, name_size, string_data_size);
        }

        std::string_view dependency_event_name{ string_data + name_start, string_data + name_start + name_size };

        size_t dummy_dependency_event_index;
        mod_context.add_dependency_event(std::string{dependency_event_name}, dependency_index, dummy_dependency_event_index);
    }

    const ReplacementV1* replacements = reinterpret_data<ReplacementV1>(data, offset, num_replacements);
    if (replacements == nullptr) {
        printf("Failed to read replacements (count: %zu)\n", num_replacements);
        return false;
    }

    for (size_t replacement_index = 0; replacement_index < num_replacements; replacement_index++) {
        N64Recomp::FunctionReplacement& cur_replacement = mod_context.replacements[replacement_index];

        cur_replacement.func_index = replacements[replacement_index].func_index;
        cur_replacement.original_section_vrom = replacements[replacement_index].original_section_vrom;
        cur_replacement.original_vram = replacements[replacement_index].original_vram;
        cur_replacement.flags = static_cast<N64Recomp::ReplacementFlags>(replacements[replacement_index].flags);
    }

    const ExportV1* exports = reinterpret_data<ExportV1>(data, offset, num_exports);
    if (exports == nullptr) {
        printf("Failed to read exports (count: %zu)\n", num_exports);
        return false;
    }

    for (size_t export_index = 0; export_index < num_exports; export_index++) {
        const ExportV1& export_in = exports[export_index];
        uint32_t func_index = export_in.func_index;
        uint32_t name_start = export_in.name_start;
        uint32_t name_size = export_in.name_size;

        if (func_index >= mod_context.functions.size()) {
            printf("Export %zu has a function index of %u, but the symbol file only has %zu functions\n",
                export_index, func_index, mod_context.functions.size());
        }

        if (name_start + name_size > string_data_size) {
            printf("Export %zu has a name start of %u and size of %u, which extend beyond the string data's total size of %zu\n",
                export_index, name_start, name_size, string_data_size);
        }

        // Add the function to the exported function list.
        mod_context.exported_funcs[export_index] = func_index;
    }

    const CallbackV1* callbacks = reinterpret_data<CallbackV1>(data, offset, num_callbacks);
    if (callbacks == nullptr) {
        printf("Failed to read callbacks (count: %zu)\n", num_callbacks);
        return false;
    }

    for (size_t callback_index = 0; callback_index < num_callbacks; callback_index++) {
        const CallbackV1& callback_in = callbacks[callback_index];
        uint32_t dependency_event_index = callback_in.dependency_event_index;
        uint32_t function_index = callback_in.function_index;

        if (dependency_event_index >= num_dependency_events) {
            printf("Callback %zu is connected to dependency event %u, but only %zu dependency events were specified\n",
                callback_index, dependency_event_index, num_dependency_events);
        }

        if (function_index >= mod_context.functions.size()) {
            printf("Callback %zu uses function %u, but only %zu functions were specified\n",
                callback_index, function_index, mod_context.functions.size());
        }

        if (!mod_context.add_callback(dependency_event_index, function_index)) {
            printf("Failed to add callback %zu\n", callback_index);
        }
    }

    const EventV1* events = reinterpret_data<EventV1>(data, offset, num_provided_events);
    if (events == nullptr) {
        printf("Failed to read events (count: %zu)\n", num_provided_events);
        return false;
    }

    for (size_t event_index = 0; event_index < num_provided_events; event_index++) {
        const EventV1& event_in = events[event_index];
        uint32_t name_start = event_in.name_start;
        uint32_t name_size = event_in.name_size;

        if (name_start + name_size > string_data_size) {
            printf("Event %zu has a name start of %u and size of %u, which extend beyond the string data's total size of %zu\n",
                event_index, name_start, name_size, string_data_size);
        }

        std::string_view import_name{ string_data + name_start, string_data + name_start + name_size };

        mod_context.add_event_symbol(std::string{import_name});
    }

    return offset == data.size();
}

N64Recomp::ModSymbolsError N64Recomp::parse_mod_symbols(std::span<const char> data, std::span<const uint8_t> binary, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, Context& mod_context_out) {
    size_t offset = 0;
    mod_context_out = {};
    const FileHeader* header = reinterpret_data<FileHeader>(data, offset);

    if (header == nullptr) {
        return ModSymbolsError::NotASymbolFile;
    }

    if (!check_magic(header)) {
        return ModSymbolsError::NotASymbolFile;
    }

    bool valid = false;

    switch (header->version) {
        case 1:
            valid = parse_v1(data, sections_by_vrom, mod_context_out);
            break;
        default:
            return ModSymbolsError::UnknownSymbolFileVersion;
    }

    if (!valid) {
        mod_context_out = {};
        return ModSymbolsError::CorruptSymbolFile;
    }

    // Fill in the words for each function.
    for (auto& cur_func : mod_context_out.functions) {
        if (cur_func.rom + cur_func.words.size() * sizeof(cur_func.words[0]) > binary.size()) {
            mod_context_out = {};
            return ModSymbolsError::FunctionOutOfBounds;
        }
        const uint32_t* func_rom = reinterpret_cast<const uint32_t*>(binary.data() + cur_func.rom);
        for (size_t word_index = 0; word_index < cur_func.words.size(); word_index++) {
            cur_func.words[word_index] = func_rom[word_index];
        }
    }

    return ModSymbolsError::Good;
}

template <typename T>
void vec_put(std::vector<uint8_t>& vec, const T* data) {
    size_t start_size = vec.size();
    vec.resize(vec.size() + sizeof(T));
    memcpy(vec.data() + start_size, data, sizeof(T));
}

void vec_put(std::vector<uint8_t>& vec, const std::string& data) {
    size_t start_size = vec.size();
    vec.resize(vec.size() + data.size());
    memcpy(vec.data() + start_size, data.data(), data.size());
}

std::vector<uint8_t> N64Recomp::symbols_to_bin_v1(const N64Recomp::Context& context) {
    std::vector<uint8_t> ret{};
    ret.reserve(1024);

    const static FileHeader header {
        .magic = {'N', '6', '4', 'R', 'S', 'Y', 'M', 'S'},
        .version = 1
    };

    vec_put(ret, &header);

    size_t num_dependencies = context.dependencies.size();
    size_t num_imported_funcs = context.import_symbols.size();
    size_t num_dependency_events = context.dependency_events.size();

    size_t num_exported_funcs = context.exported_funcs.size();
    size_t num_events = context.event_symbols.size();
    size_t num_callbacks = context.callbacks.size();
    size_t num_provided_events = context.event_symbols.size();

    FileSubHeaderV1 sub_header {
        .num_sections = static_cast<uint32_t>(context.sections.size()),
        .num_dependencies = static_cast<uint32_t>(num_dependencies),
        .num_imports = static_cast<uint32_t>(num_imported_funcs),
        .num_dependency_events = static_cast<uint32_t>(num_dependency_events),
        .num_replacements = static_cast<uint32_t>(context.replacements.size()),
        .num_exports = static_cast<uint32_t>(num_exported_funcs),
        .num_callbacks = static_cast<uint32_t>(num_callbacks),
        .num_provided_events = static_cast<uint32_t>(num_provided_events),
        .string_data_size = 0,
    };

    // Record the sub-header offset so the string data size can be filled in later.
    size_t sub_header_offset = ret.size();
    vec_put(ret, &sub_header);

    // Build the string data from the exports and imports.
    size_t strings_start = ret.size();

    // Track the start of every dependency's name in the string data.
    std::vector<uint32_t> dependency_name_positions{};
    dependency_name_positions.resize(num_dependencies);
    for (size_t dependency_index = 0; dependency_index < num_dependencies; dependency_index++) {
        const Dependency& dependency = context.dependencies[dependency_index];

        dependency_name_positions[dependency_index] = static_cast<uint32_t>(ret.size() - strings_start);
        vec_put(ret, dependency.mod_id);
    }

    // Track the start of every imported function's name in the string data.
    std::vector<uint32_t> imported_func_name_positions{};
    imported_func_name_positions.resize(num_imported_funcs);
    for (size_t import_index = 0; import_index < num_imported_funcs; import_index++) {
        const ImportSymbol& imported_func = context.import_symbols[import_index];

        // Write this import's name into the strings data.
        imported_func_name_positions[import_index] = static_cast<uint32_t>(ret.size() - strings_start);
        vec_put(ret, imported_func.base.name);
    }

    // Track the start of every dependency event's name in the string data.
    std::vector<uint32_t> dependency_event_name_positions{};
    dependency_event_name_positions.resize(num_dependency_events);
    for (size_t dependency_event_index = 0; dependency_event_index < num_dependency_events; dependency_event_index++) {
        const DependencyEvent& dependency_event = context.dependency_events[dependency_event_index];

        dependency_event_name_positions[dependency_event_index] = static_cast<uint32_t>(ret.size() - strings_start);
        vec_put(ret, dependency_event.event_name);
    }
    
    // Track the start of every exported function's name in the string data.
    std::vector<uint32_t> exported_func_name_positions{};
    exported_func_name_positions.resize(num_exported_funcs);
    for (size_t export_index = 0; export_index < num_exported_funcs; export_index++) {
        size_t function_index = context.exported_funcs[export_index];
        const Function& exported_func = context.functions[function_index];

        exported_func_name_positions[export_index] = static_cast<uint32_t>(ret.size() - strings_start);
        vec_put(ret, exported_func.name);
    }

    // Track the start of every provided event's name in the string data.
    std::vector<uint32_t> event_name_positions{};
    event_name_positions.resize(num_events);
    for (size_t event_index = 0; event_index < num_events; event_index++) {
        const EventSymbol& event_symbol = context.event_symbols[event_index];

        // Write this event's name into the strings data.
        event_name_positions[event_index] = static_cast<uint32_t>(ret.size() - strings_start);
        vec_put(ret, event_symbol.base.name);
    }

    // Align the data after the strings to 4 bytes.
    size_t strings_size = round_up_4(ret.size() - strings_start);
    ret.resize(strings_size + strings_start);

    // Fill in the string data size in the sub-header.
    reinterpret_cast<FileSubHeaderV1*>(ret.data() + sub_header_offset)->string_data_size = strings_size;

    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        const Section& cur_section = context.sections[section_index];
        SectionHeaderV1 section_out {
            .file_offset = cur_section.rom_addr,
            .vram = cur_section.ram_addr,
            .rom_size = cur_section.size,
            .bss_size = cur_section.bss_size,
            .num_funcs = static_cast<uint32_t>(context.section_functions[section_index].size()),
            .num_relocs = static_cast<uint32_t>(cur_section.relocs.size())
        };

        vec_put(ret, &section_out);

        for (size_t func_index : context.section_functions[section_index]) {
            const Function& cur_func = context.functions[func_index];
            FuncV1 func_out {
                .section_offset = cur_func.vram - cur_section.ram_addr,
                .size = (uint32_t)(cur_func.words.size() * sizeof(cur_func.words[0])) 
            };

            vec_put(ret, &func_out);
        }

        for (size_t reloc_index = 0; reloc_index < cur_section.relocs.size(); reloc_index++) {
            const Reloc& cur_reloc = cur_section.relocs[reloc_index];
            uint32_t target_section_vrom;
            uint32_t target_section_offset_or_index = cur_reloc.target_section_offset;
            if (cur_reloc.target_section == SectionAbsolute) {
                printf("Internal error: reloc %zu in section %zu references an absolute symbol and should have been relocated already. Please report this issue.\n",
                    reloc_index, section_index);
                return {};
            }
            else if (cur_reloc.target_section == SectionImport) {
                target_section_vrom = SectionImportVromV1;
                target_section_offset_or_index = cur_reloc.symbol_index;
            }
            else if (cur_reloc.target_section == SectionEvent) {
                target_section_vrom = SectionEventVromV1;
                target_section_offset_or_index = cur_reloc.symbol_index;
            }
            else if (cur_reloc.reference_symbol) {
                target_section_vrom = context.get_reference_section_rom(cur_reloc.target_section);
            }
            else {
                if (cur_reloc.target_section >= context.sections.size()) {
                    printf("Internal error: reloc %zu in section %zu references section %u, but only %zu exist. Please report this issue.\n",
                        reloc_index, section_index, cur_reloc.target_section, context.sections.size());
                    return {};
                }
                target_section_vrom = SectionSelfVromFlagV1 | cur_reloc.target_section;
            }
            RelocV1 reloc_out {
                .section_offset = cur_reloc.address - cur_section.ram_addr,
                .type = static_cast<uint32_t>(cur_reloc.type),
                .target_section_offset_or_index = target_section_offset_or_index,
                .target_section_vrom = target_section_vrom
            };

            vec_put(ret, &reloc_out);
        }
    }

    // Write the dependencies.
    for (size_t dependency_index = 0; dependency_index < num_dependencies; dependency_index++) {
        const Dependency& dependency = context.dependencies[dependency_index];

        DependencyV1 dependency_out {
            .major_version = dependency.major_version,
            .minor_version = dependency.minor_version,
            .patch_version = dependency.patch_version,
            .mod_id_start = dependency_name_positions[dependency_index],
            .mod_id_size = static_cast<uint32_t>(dependency.mod_id.size())
        };

        vec_put(ret, &dependency_out);
    }

    // Write the imported functions.
    for (size_t import_index = 0; import_index < num_imported_funcs; import_index++) {
        // Get the index of the reference symbol for this import.
        const ImportSymbol& imported_func = context.import_symbols[import_index];

        ImportV1 import_out {
            .name_start = imported_func_name_positions[import_index],
            .name_size = static_cast<uint32_t>(imported_func.base.name.size()),
            .dependency = static_cast<uint32_t>(imported_func.dependency_index)
        };

        vec_put(ret, &import_out);
    }

    // Write the dependency events.
    for (size_t dependency_event_index = 0; dependency_event_index < num_dependency_events; dependency_event_index++) {
        const DependencyEvent& dependency_event = context.dependency_events[dependency_event_index];

        DependencyEventV1 dependency_event_out {
            .name_start = dependency_event_name_positions[dependency_event_index],
            .name_size = static_cast<uint32_t>(dependency_event.event_name.size()),
            .dependency = static_cast<uint32_t>(dependency_event.dependency_index)
        };

        vec_put(ret, &dependency_event_out);
    }

    // Write the function replacements.
    for (const FunctionReplacement& cur_replacement : context.replacements) {
        uint32_t flags = 0;
        if ((cur_replacement.flags & ReplacementFlags::Force) == ReplacementFlags::Force) {
            flags |= 0x1;
        }

        ReplacementV1 replacement_out {
            .func_index = cur_replacement.func_index,
            .original_section_vrom = cur_replacement.original_section_vrom,
            .original_vram = cur_replacement.original_vram,
            .flags = flags
        };

        vec_put(ret, &replacement_out);
    };

    // Write the exported functions.
    for (size_t export_index = 0; export_index < num_exported_funcs; export_index++) {
        size_t function_index = context.exported_funcs[export_index];
        const Function& exported_func = context.functions[function_index];

        ExportV1 export_out {
            .func_index = static_cast<uint32_t>(function_index),
            .name_start = exported_func_name_positions[export_index],
            .name_size = static_cast<uint32_t>(exported_func.name.size())
        };

        vec_put(ret, &export_out);
    }

    // Write the callbacks.
    for (size_t callback_index = 0; callback_index < num_callbacks; callback_index++) {
        const Callback& callback = context.callbacks[callback_index];

        CallbackV1 callback_out {
            .dependency_event_index = static_cast<uint32_t>(callback.dependency_event_index),
            .function_index = static_cast<uint32_t>(callback.function_index)
        };

        vec_put(ret, &callback_out);
    }

    // Write the provided events.
    for (size_t event_index = 0; event_index < num_events; event_index++) {
        const EventSymbol& event_symbol = context.event_symbols[event_index];

        EventV1 event_out {
            .name_start = event_name_positions[event_index],
            .name_size = static_cast<uint32_t>(event_symbol.base.name.size())
        };

        vec_put(ret, &event_out);
    }

    return ret;
}
