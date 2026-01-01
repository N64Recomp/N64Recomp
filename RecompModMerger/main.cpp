#include <cstdio>
#include <fstream>

#include "recompiler/context.h"

template <typename T>
bool read_file(const std::filesystem::path& p, std::vector<T>& out) {
    static_assert(sizeof(T) == 1);
    std::vector<T> ret{};

    std::ifstream input_file{p, std::ios::binary};
    if (!input_file.good()) {
        return false;
    }
    
    input_file.seekg(0, std::ios::end);
    ret.resize(input_file.tellg());
    input_file.seekg(0, std::ios::beg);

    input_file.read(reinterpret_cast<char*>(ret.data()), ret.size());

    out = std::move(ret);

    return true;
}

bool write_file(const std::filesystem::path& p, std::span<char> in) {
    std::ofstream out{ p, std::ios::binary };
    if (!out.good()) {
        return false;
    }

    out.write(in.data(), in.size());
    return true;
}

std::span<uint8_t> reinterpret_span_u8(std::span<char> s) {
    return std::span(reinterpret_cast<uint8_t*>(s.data()), s.size());
}

std::span<char> reinterpret_span_char(std::span<uint8_t> s) {
    return std::span(reinterpret_cast<char*>(s.data()), s.size());
}

bool copy_into_context(N64Recomp::Context& out, const N64Recomp::Context& in) {
    size_t rom_offset = out.rom.size();
    size_t section_offset = out.sections.size();
    size_t function_offset = out.functions.size();
    size_t event_offset = out.event_symbols.size();
    
    // Append the input rom to the end of the output rom.
    out.rom.insert(out.rom.end(), in.rom.begin(), in.rom.end());

    // Merge dependencies from the input. Copy new ones and remap existing ones.
    std::vector<size_t> new_dependency_indices(in.dependencies.size());
    for (size_t dep_index = 0; dep_index < in.dependencies.size(); dep_index++) {
        const std::string& dep = in.dependencies[dep_index];
        auto find_dep_it = out.dependencies_by_name.find(dep);
        if (find_dep_it != out.dependencies_by_name.end()) {
            new_dependency_indices[dep_index] = find_dep_it->second;
        }
        else {
            out.dependencies_by_name[dep] = out.dependencies.size();
            new_dependency_indices[dep_index] = out.dependencies.size();
            out.dependencies.emplace_back(dep);
        }
    }

    // Merge imports from the input. Copy new ones and remap existing ones.
    std::vector<size_t> new_import_indices(in.import_symbols.size());
    for (size_t import_index = 0; import_index < in.import_symbols.size(); import_index++) {
        const N64Recomp::ImportSymbol& sym = in.import_symbols[import_index];
        size_t dependency_index = new_dependency_indices[sym.dependency_index];

        size_t original_import_index = (size_t)-1;
        
        // Check if any import symbols have the same dependency index and symbol name.
        for (size_t i = 0; i < out.import_symbols.size(); i++) {
            const N64Recomp::ImportSymbol& sym_out = out.import_symbols[i];
            if (sym_out.dependency_index == dependency_index && sym_out.base.name == sym.base.name) {
                original_import_index = i;
                break;
            }
        }

        if (original_import_index != (size_t)-1) {
            new_import_indices[import_index] = original_import_index;
        }
        else {
            new_import_indices[import_index] = out.import_symbols.size();
            N64Recomp::ImportSymbol new_sym{};
            new_sym.dependency_index = dependency_index;
            new_sym.base.name = sym.base.name;
            out.import_symbols.emplace_back(std::move(new_sym));
        }
    }

    // Merge dependency events from the input. Copy new ones and remap existing ones.
    std::vector<size_t> new_dependency_event_indices(in.dependency_events.size());
    for (size_t dependency_event_index = 0; dependency_event_index < in.dependency_events.size(); dependency_event_index++) {
        const N64Recomp::DependencyEvent& event = in.dependency_events[dependency_event_index];
        size_t dependency_index = new_dependency_indices[event.dependency_index];

        size_t original_event_index = (size_t)-1;

        // Check if any dependency events have the same dependency index and event name.
        for (size_t i = 0; i < out.dependency_events.size(); i++) {
            const N64Recomp::DependencyEvent& event_out = out.dependency_events[i];
            if (event_out.dependency_index == dependency_index && event_out.event_name == event.event_name) {
                original_event_index = i;
                break;
            }
        }

        if (original_event_index != (size_t)-1) {
            new_dependency_event_indices[dependency_event_index] = original_event_index;
        }
        else {
            new_dependency_event_indices[dependency_event_index] = out.dependency_events.size();
            out.dependency_events.emplace_back(N64Recomp::DependencyEvent{ .dependency_index = dependency_index, .event_name = event.event_name });
        }
    }

    // Copy every section from the input.
    for (size_t section_index = 0; section_index < in.sections.size(); section_index++) {
        const N64Recomp::Section& section = in.sections[section_index];

        size_t out_section_index = section_offset + section_index;
        N64Recomp::Section& section_out = out.sections.emplace_back(section);
        section_out.rom_addr += rom_offset;
        section_out.name = "";

        // Adjust the section index of all the section's relocs.
        for (N64Recomp::Reloc& reloc : section_out.relocs) {
            if (reloc.target_section == N64Recomp::SectionAbsolute) {
                printf("Internal error: reloc in section %zu references an absolute symbol and should have been relocated already. Please report this issue.\n",
                    section_index);
                // Nothing to do for absolute relocs.
            }
            else if (reloc.target_section == N64Recomp::SectionImport) {
                // symbol_index indexes context.import_symbols
                reloc.symbol_index = new_import_indices[reloc.symbol_index];
            }
            else if (reloc.target_section == N64Recomp::SectionEvent) {
                // symbol_index indexes context.event_symbols
                reloc.symbol_index += event_offset;
            }
            else if (reloc.reference_symbol) {
                // symbol_index indexes context.reference_symbols
                // Nothing to do here, reference section indices will remain unchanged.
            }
            else {
                reloc.target_section += section_offset;
            }
        }
    }

    out.section_functions.resize(out.sections.size());

    // Copy every function from the input.
    for (size_t func_index = 0; func_index < in.functions.size(); func_index++) {
        const N64Recomp::Function& func = in.functions[func_index];

        size_t out_func_index = function_offset + func_index;
        N64Recomp::Function& function_out = out.functions.emplace_back(func);

        function_out.section_index += section_offset;
        function_out.rom += rom_offset;
        // functions_by_name unused
        out.functions_by_vram[function_out.vram].push_back(out_func_index);

        out.section_functions[function_out.section_index].push_back(out_func_index);
    }

    // Copy replacements from the input.
    for (size_t replacement_index = 0; replacement_index < in.replacements.size(); replacement_index++) {
        const N64Recomp::FunctionReplacement& replacement = in.replacements[replacement_index];
        N64Recomp::FunctionReplacement& replacement_out = out.replacements.emplace_back(replacement);
        replacement_out.func_index += function_offset;
    }

    // Copy hooks from the input.
    for (size_t hook_index = 0; hook_index < in.hooks.size(); hook_index++) {
        const N64Recomp::FunctionHook& hook = in.hooks[hook_index];
        N64Recomp::FunctionHook& hook_out = out.hooks.emplace_back(hook);
        hook_out.func_index += function_offset;
    }

    // Copy callbacks from the input.
    for (size_t callback_index = 0; callback_index < in.callbacks.size(); callback_index++) {
        const N64Recomp::Callback& callback = in.callbacks[callback_index];
        N64Recomp::Callback callback_out = out.callbacks.emplace_back(callback);
        callback_out.dependency_event_index = new_dependency_event_indices[callback_out.dependency_event_index];
    }

    // Copy exports from the input.
    for (size_t exported_func : in.exported_funcs) {
        out.exported_funcs.push_back(exported_func + function_offset);
    }

    // Copy events from the input.
    for (const N64Recomp::EventSymbol& event_sym : in.event_symbols) {
        out.event_symbols.emplace_back(event_sym);
    }

    return true;
}

int main(int argc, const char** argv) {
    if (argc != 8) {
        printf("Usage: %s <function symbol toml> <symbol file 1> <binary 1> <symbol file 2> <binary 2> <output symbol file> <output binary file>\n", argv[0]);
        return EXIT_SUCCESS;
    }

    const char* function_symbol_toml_path = argv[1];
    const char* sym_file_path_1 = argv[2];
    const char* binary_path_1 = argv[3];
    const char* sym_file_path_2 = argv[4];
    const char* binary_path_2 = argv[5];
    const char* output_sym_path = argv[6];
    const char* output_binary_path = argv[7];

    // Load the symbol and binary files.
    std::vector<char> sym_file_1;
    if (!read_file(sym_file_path_1, sym_file_1)) {
        fprintf(stderr, "Error reading file %s\n", sym_file_path_1);
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> binary_1;
    if (!read_file(binary_path_1, binary_1)) {
        fprintf(stderr, "Error reading file %s\n", binary_path_1);
        return EXIT_FAILURE;
    }

    std::vector<char> sym_file_2;
    if (!read_file(sym_file_path_2, sym_file_2)) {
        fprintf(stderr, "Error reading file %s\n", sym_file_path_2);
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> binary_2;
    if (!read_file(binary_path_2, binary_2)) {
        fprintf(stderr, "Error reading file %s\n", binary_path_2);
        return EXIT_FAILURE;
    }
    
    N64Recomp::ModSymbolsError err;

    // Parse the symbol toml.
    std::vector<uint8_t> dummy_rom{};
    N64Recomp::Context reference_context{};
    if (!N64Recomp::Context::from_symbol_file(function_symbol_toml_path, std::move(dummy_rom), reference_context, false)) {
        fprintf(stderr, "Failed to load provided function reference symbol file\n");
        return EXIT_FAILURE;
    }

    // Build a reference section lookup of rom address.
    std::unordered_map<uint32_t, uint16_t> sections_by_rom{};
    for (size_t section_index = 0; section_index < reference_context.sections.size(); section_index++) {
        sections_by_rom[reference_context.sections[section_index].rom_addr] = section_index;
    }

    // Parse the two contexts.
    N64Recomp::Context context1{};
    err = N64Recomp::parse_mod_symbols(sym_file_1, binary_1, sections_by_rom, context1);
    if (err != N64Recomp::ModSymbolsError::Good) {
        fprintf(stderr, "Error parsing mod symbols %s\n", sym_file_path_1);
        return EXIT_FAILURE;
    }
    context1.rom = std::move(binary_1);

    N64Recomp::Context context2{};
    err = N64Recomp::parse_mod_symbols(sym_file_2, binary_2, sections_by_rom, context2);
    if (err != N64Recomp::ModSymbolsError::Good) {
        fprintf(stderr, "Error parsing mod symbols %s\n", sym_file_path_2);
        return EXIT_FAILURE;
    }
    context2.rom = std::move(binary_2);

    N64Recomp::Context merged{};
    merged.import_reference_context(reference_context);

    if (!copy_into_context(merged, context1)) {
        fprintf(stderr, "Failed to merge first mod into output\n");
        return EXIT_FAILURE;
    }
    if (!copy_into_context(merged, context2)) {
        fprintf(stderr, "Failed to merge second mod into output\n");
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> syms_out = N64Recomp::symbols_to_bin_v1(merged);

    if (!write_file(output_sym_path, reinterpret_span_char(syms_out))) {
        fprintf(stderr, "Failed to write symbol file to %s\n", output_sym_path);
        return EXIT_FAILURE;
    }

    if (!write_file(output_binary_path, reinterpret_span_char(std::span{ merged.rom }))) {
        fprintf(stderr, "Failed to write binary file to %s\n", output_binary_path);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
