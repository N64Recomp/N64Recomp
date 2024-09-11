#include <array>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <cctype>
#include "fmt/format.h"
#include "fmt/ostream.h"
#include "n64recomp.h"
#include <toml++/toml.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

constexpr std::string_view symbol_filename = "mod_syms.bin";
constexpr std::string_view binary_filename = "mod_binary.bin";
constexpr std::string_view manifest_filename = "manifest.json";

struct ModManifest {
    std::string mod_id;
    std::string version_string;
    std::vector<std::string> authors;
    std::string game_id;
    std::string minimum_recomp_version;
    std::unordered_map<std::string, std::vector<std::string>> native_libraries;
    std::vector<std::string> dependencies;
    std::vector<std::string> full_dependency_strings;
};

struct ModInputs {
    std::filesystem::path elf_path;
    std::filesystem::path func_reference_syms_file_path;
    std::vector<std::filesystem::path> data_reference_syms_file_paths;
    std::vector<std::filesystem::path> additional_files;
};

struct ModConfig {
    ModManifest manifest;
    ModInputs inputs;
};

static std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
    if (child.is_absolute()) {
        return child;
    }
    if (!child.empty()) {
        return parent / child;
    }
    return child;
}

static bool validate_version_string(std::string_view str, bool& has_label) {
    std::array<size_t, 2> period_indices;
    size_t num_periods = 0;
    size_t cur_pos = 0;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;

    // Find the 2 required periods.
    cur_pos = str.find('.', cur_pos);
    period_indices[0] = cur_pos;
    cur_pos = str.find('.', cur_pos + 1);
    period_indices[1] = cur_pos;

    // Check that both were found.
    if (period_indices[0] == std::string::npos || period_indices[1] == std::string::npos) {
        return false;
    }

    // Parse the 3 numbers formed by splitting the string via the periods.
    std::array<std::from_chars_result, 3> parse_results; 
    std::array<size_t, 3> parse_starts { 0, period_indices[0] + 1, period_indices[1] + 1 };
    std::array<size_t, 3> parse_ends { period_indices[0], period_indices[1], str.size() };
    parse_results[0] = std::from_chars(str.data() + parse_starts[0], str.data() + parse_ends[0], major);
    parse_results[1] = std::from_chars(str.data() + parse_starts[1], str.data() + parse_ends[1], minor);
    parse_results[2] = std::from_chars(str.data() + parse_starts[2], str.data() + parse_ends[2], patch);

    // Check that the first two parsed correctly.
    auto did_parse = [&](size_t i) {
        return parse_results[i].ec == std::errc{} && parse_results[i].ptr == str.data() + parse_ends[i];
    };
    
    if (!did_parse(0) || !did_parse(1)) {
        return false;
    }

    // Check that the third had a successful parse, but not necessarily read all the characters.
    if (parse_results[2].ec != std::errc{}) {
        return false;
    }

    // Allow a plus or minus directly after the third number.
    if (parse_results[2].ptr != str.data() + parse_ends[2]) {
        has_label = true;
        if (*parse_results[2].ptr != '+' && *parse_results[2].ptr != '-') {
            // Failed to parse, as nothing is allowed directly after the last number besides a plus or minus.
            return false;
        }
    }
    else {
        has_label = false;
    }

    return true;
}

static bool validate_dependency_string(const std::string& val, size_t& name_length, bool& has_label) {
    std::string ret;
    size_t name_length_temp;

    // Don't allow an empty dependency name.
    if (val.size() == 0) {
        return false;
    }
    bool validated_name;
    bool validated_version;

    // Check if there's a version number specified.
    size_t colon_pos = val.find(':');
    if (colon_pos == std::string::npos) {
        // No version present, so just validate the dependency's id.

        validated_name = N64Recomp::validate_mod_id(std::string_view{val});
        name_length_temp = val.size();
        validated_version = true;
        has_label = false;
    }
    else {
        // Version present, validate it.

        // Don't allow an empty dependency name after accounting for the colon.
        if (colon_pos == 0) {
            return false;
        }
        
        name_length_temp = colon_pos;
        
        // Validate the dependency's id and version.
        validated_name = N64Recomp::validate_mod_id(std::string_view{val.begin(), val.begin() + colon_pos});
        validated_version = validate_version_string(std::string_view{val.begin() + colon_pos + 1, val.end()}, has_label);
    }

    if (validated_name && validated_version) {
        name_length = name_length_temp;
        return true;
    }

    return false;
}

template <typename T>
static T read_toml_value(const toml::table& data, std::string_view key, bool required) {
    const toml::node* value_node  = data.get(key);

    if (value_node == nullptr) {
        if (required) {
            throw toml::parse_error(("Missing required field " + std::string{key}).c_str(), data.source());
        }
        else {
            return T{};
        }
    }

    std::optional<T> opt = value_node->value_exact<T>();
    if (opt.has_value()) {
        return opt.value();
    }
    else {
        throw toml::parse_error(("Incorrect type for field " + std::string{key}).c_str(), data.source());
    }
}

static const toml::array& read_toml_array(const toml::table& data, std::string_view key, bool required) {
    static const toml::array empty_array = toml::array{};
    const toml::node* value_node = data.get(key);

    if (value_node == nullptr) {
        if (required) {
            throw toml::parse_error(("Missing required field " + std::string{ key }).c_str(), data.source());
        }
        else {
            return empty_array;
        }
    }

    if (!value_node->is_array()) {
        throw toml::parse_error(("Incorrect type for field " + std::string{ key }).c_str(), value_node->source());
    }

    return *value_node->as_array();
}

static std::vector<std::filesystem::path> get_toml_path_array(const toml::array& toml_array, const std::filesystem::path& basedir) {
    std::vector<std::filesystem::path> ret;

    // Reserve room for all the funcs in the map.
    ret.reserve(toml_array.size());
    toml_array.for_each([&ret, &basedir](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
            ret.emplace_back(concat_if_not_empty(basedir, el.template ref<std::string>()));
        }
        else {
            throw toml::parse_error("Invalid type for file entry", el.source());
        }
    });

    return ret;
}

ModManifest parse_mod_config_manifest(const std::filesystem::path& basedir, const toml::table& manifest_table) {
    ModManifest ret;

    // Mod ID
    ret.mod_id = read_toml_value<std::string_view>(manifest_table, "id", true);

    // Mod version
    ret.version_string = read_toml_value<std::string_view>(manifest_table, "version", true);
    bool version_has_label;
    if (!validate_version_string(ret.version_string, version_has_label)) {
        throw toml::parse_error("Invalid mod version", manifest_table["version"].node()->source());
    }

    // Authors
    const toml::array& authors_array = read_toml_array(manifest_table, "authors", true);
    authors_array.for_each([&ret](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
            ret.authors.emplace_back(el.template ref<std::string>());
        }
        else {
            throw toml::parse_error("Invalid type for author entry", el.source());
        }
    });
   
    // Game ID
    ret.game_id = read_toml_value<std::string_view>(manifest_table, "game_id", true);

    // Minimum recomp version
    ret.minimum_recomp_version = read_toml_value<std::string_view>(manifest_table, "minimum_recomp_version", true);
    bool minimum_recomp_version_has_label;
    if (!validate_version_string(ret.minimum_recomp_version, minimum_recomp_version_has_label)) {
        throw toml::parse_error("Invalid minimum recomp version", manifest_table["minimum_recomp_version"].node()->source());
    }
    if (minimum_recomp_version_has_label) {
        throw toml::parse_error("Minimum recomp version may not have a label", manifest_table["minimum_recomp_version"].node()->source());
    }

    // Native libraries (optional)
    const toml::array& native_libraries = read_toml_array(manifest_table, "native_libraries", false);
    if (!native_libraries.empty()) {
        native_libraries.for_each([&ret](const auto& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                const toml::table& el_table = *el.as_table();
                std::string_view library_name = read_toml_value<std::string_view>(el_table, "name", true);
                const toml::array funcs_array = read_toml_array(el_table, "funcs", true);
                std::vector<std::string> cur_funcs{};
                funcs_array.for_each([&ret, &cur_funcs](const auto& func_el) {
                    if constexpr (toml::is_string<decltype(func_el)>) {
                        cur_funcs.emplace_back(func_el.template ref<std::string>());
                    }
                    else {
                        throw toml::parse_error("Invalid type for native library function entry", func_el.source());
                    }
                });
                ret.native_libraries.emplace(std::string{library_name}, std::move(cur_funcs));
            }
            else {
                throw toml::parse_error("Invalid type for native library entry", el.source());
            }
        });
    }

    // Dependency list (optional)
    const toml::array& dependency_array = read_toml_array(manifest_table, "dependencies", false);
    if (!dependency_array.empty()) {
        // Reserve room for all the dependencies.
        ret.dependencies.reserve(dependency_array.size());
        dependency_array.for_each([&ret](const auto& el) {
            if constexpr (toml::is_string<decltype(el)>) {
                size_t dependency_id_length;
                bool dependency_version_has_label;
                if (!validate_dependency_string(el.template ref<std::string>(), dependency_id_length, dependency_version_has_label)) {
                    throw toml::parse_error("Invalid dependency entry", el.source());
                }
                if (dependency_version_has_label) {
                    throw toml::parse_error("Dependency versions may not have labels", el.source());
                }
                std::string dependency_id = el.template ref<std::string>().substr(0, dependency_id_length);
                ret.dependencies.emplace_back(dependency_id);
                ret.full_dependency_strings.emplace_back(el.template ref<std::string>());
            }
            else {
                throw toml::parse_error("Invalid type for dependency entry", el.source());
            }
        });
    }

    return ret;
}

ModInputs parse_mod_config_inputs(const std::filesystem::path& basedir, const toml::table& inputs_table) {
    ModInputs ret;

    // Elf file
    std::optional<std::string> elf_path_opt = inputs_table["elf_path"].value<std::string>();
    if (elf_path_opt.has_value()) {
        ret.elf_path = concat_if_not_empty(basedir, elf_path_opt.value());
    }
    else {
        throw toml::parse_error("Mod toml input section is missing elf file", inputs_table.source());
    }

    // Function reference symbols file
    std::optional<std::string> func_reference_syms_file_opt = inputs_table["func_reference_syms_file"].value<std::string>();
    if (func_reference_syms_file_opt.has_value()) {
        ret.func_reference_syms_file_path = concat_if_not_empty(basedir, func_reference_syms_file_opt.value());
    }
    else {
        throw toml::parse_error("Mod toml input section is missing function reference symbol file", inputs_table.source());
    }
    
    // Data reference symbols files
    toml::node_view data_reference_syms_file_data = inputs_table["data_reference_syms_files"];
    if (data_reference_syms_file_data.is_array()) {
        const toml::array& array = *data_reference_syms_file_data.as_array();
        ret.data_reference_syms_file_paths = get_toml_path_array(array, basedir);
    }
    else {
        if (data_reference_syms_file_data) {
            throw toml::parse_error("Mod toml input section is missing data reference symbol file list", inputs_table.source());
        }
        else {
            throw toml::parse_error("Invalid data reference symbol file list", data_reference_syms_file_data.node()->source());
        }
    }

    // Additional files (optional)
    const toml::array& additional_files_array = read_toml_array(inputs_table, "additional_files", false);
    if (!additional_files_array.empty()) {
        ret.additional_files = get_toml_path_array(additional_files_array, basedir);
    }
    
    return ret;
}

ModConfig parse_mod_config(const std::filesystem::path& config_path, bool& good) {
    ModConfig ret{};
    good = false;

    toml::table toml_data{};

    try {
        toml_data = toml::parse_file(config_path.native());
        std::filesystem::path basedir = config_path.parent_path();
        
        // Find the manifest section and validate its type.
        const toml::node* manifest_data_ptr = toml_data.get("manifest");
        if (manifest_data_ptr == nullptr) {
            throw toml::parse_error("Mod toml is missing manifest section", toml::source_region{});
        }
        if (!manifest_data_ptr->is_table()) {
            throw toml::parse_error("Incorrect type for mod toml manifest section", manifest_data_ptr->source());
        }
        const toml::table& manifest_table = *manifest_data_ptr->as_table();

        // Find the inputs section and validate its type.
        const toml::node* inputs_data_ptr = toml_data.get("inputs");
        if (inputs_data_ptr == nullptr) {
            throw toml::parse_error("Mod toml is missing inputs section", toml::source_region{});
        }
        if (!inputs_data_ptr->is_table()) {
            throw toml::parse_error("Incorrect type for mod toml inputs section", inputs_data_ptr->source());
        }
        const toml::table& inputs_table = *inputs_data_ptr->as_table();

        // Parse the manifest.
        ret.manifest = parse_mod_config_manifest(basedir, manifest_table);
        // Parse the inputs.
        ret.inputs = parse_mod_config_inputs(basedir, inputs_table);
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << config_path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return {};
    }

    good = true;
    return ret;
}

static inline uint32_t round_up_16(uint32_t value) {
    return (value + 15) & (~15);
}

bool parse_callback_name(std::string_view data, std::string& dependency_name, std::string& event_name) {
    size_t period_pos = data.find(':');

    if (period_pos == std::string::npos) {
        return false;
    }

    std::string_view dependency_name_view = std::string_view{data}.substr(0, period_pos);
    std::string_view event_name_view = std::string_view{data}.substr(period_pos + 1);

    if (!N64Recomp::validate_mod_id(dependency_name_view)) {
        return false;
    }

    dependency_name = dependency_name_view;
    event_name = event_name_view;
    return true;
}

void print_vector_elements(std::ostream& output_file, const std::vector<std::string>& vec, bool compact) {
    char separator = compact ? ' ' : '\n';
    for (size_t i = 0; i < vec.size(); i++) {
        const std::string& val = vec[i];
        fmt::print(output_file, "{}\"{}\"{}{}",
            compact ? "" : "        ", val, i == vec.size() - 1 ? "" : ",", separator);
    }
}

void write_manifest(const std::filesystem::path& path, const ModManifest& manifest) {
    std::ofstream output_file(path);

    fmt::print(output_file,
        "{{\n"
        "    \"game_id\": \"{}\",\n"
        "    \"id\": \"{}\",\n"
        "    \"version\": \"{}\",\n"
        "    \"authors\": [\n",
        manifest.game_id, manifest.mod_id, manifest.version_string);

    print_vector_elements(output_file, manifest.authors, false);

    fmt::print(output_file, 
        "    ],\n"
        "    \"minimum_recomp_version\": \"{}\"",
        manifest.minimum_recomp_version);

    if (!manifest.native_libraries.empty()) {
        fmt::print(output_file, ",\n"
            "    \"native_libraries\": {{\n");
        size_t library_index = 0; 
        for (const auto& [library, funcs] : manifest.native_libraries) {
            fmt::print(output_file, "        \"{}\": [ ",
                library);
            print_vector_elements(output_file, funcs, true);
            fmt::print(output_file, "]{}\n",
                library_index == manifest.native_libraries.size() - 1 ? "" : ",");
            library_index++;
        }
        fmt::print(output_file, "    }}");
    }

    if (!manifest.full_dependency_strings.empty()) {
        fmt::print(output_file, ",\n"
            "    \"dependencies\": [\n");
        print_vector_elements(output_file, manifest.full_dependency_strings, false);
        fmt::print(output_file, "    ]");
    }


    fmt::print(output_file, "\n}}\n");
}

N64Recomp::Context build_mod_context(const N64Recomp::Context& input_context, bool& good) {
    N64Recomp::Context ret{};
    good = false;

    // Make a vector containing 0, 1, 2, ... section count - 1
    std::vector<uint16_t> section_order;
    section_order.resize(input_context.sections.size());
    std::iota(section_order.begin(), section_order.end(), 0);

    // TODO this sort is currently disabled because sections seem to already be ordered
    // by elf offset. Determine if this is always the case and remove this if so.
    //// Sort the vector based on the rom address of the corresponding section.
    //std::sort(section_order.begin(), section_order.end(),
    //    [&](uint16_t a, uint16_t b) {
    //        const auto& section_a = input_context.sections[a];
    //        const auto& section_b = input_context.sections[b];
    //        // Sort primarily by ROM address.
    //        if (section_a.rom_addr != section_b.rom_addr) {
    //            return section_a.rom_addr < section_b.rom_addr;
    //        }
    //        // Sort secondarily by RAM address.
    //        return section_a.ram_addr < section_b.ram_addr;
    //    }
    //);

    // TODO avoid a copy here.
    ret.rom = input_context.rom;

    // Copy the dependency data from the input context.
    ret.dependencies_by_name = input_context.dependencies_by_name;
    ret.import_symbols = input_context.import_symbols;
    ret.dependency_events = input_context.dependency_events;
    ret.dependency_events_by_name = input_context.dependency_events_by_name;
    ret.dependency_imports_by_name = input_context.dependency_imports_by_name;

    uint32_t rom_to_ram = (uint32_t)-1;
    size_t output_section_index = (size_t)-1;
    ret.sections.resize(1);

    // Mapping of input section to output section for fixing up relocations.
    std::unordered_map<uint16_t, uint16_t> input_section_to_output_section{};

    // Iterate over the input sections in their sorted order.
    for (uint16_t section_index : section_order) {
        const auto& cur_section = input_context.sections[section_index];
        uint32_t cur_rom_to_ram = cur_section.ram_addr - cur_section.rom_addr;

        // Check if this is a non-allocated section.
        if (cur_section.rom_addr == (uint32_t)-1) {
            // If so, check if it has a vram address directly after the current output section. If it does, then add this
            // section's size to the output section's bss size.
            if (output_section_index != -1 && cur_section.size != 0) {
                auto& section_out = ret.sections[output_section_index];
                uint32_t output_section_bss_start = section_out.ram_addr + section_out.size;
                uint32_t output_section_bss_end = output_section_bss_start + section_out.bss_size;
                // Check if the current section starts at the end of the output section, allowing for a range of matches to account for 16 byte section alignment.
                if (cur_section.ram_addr >= output_section_bss_end && cur_section.ram_addr <= round_up_16(output_section_bss_end)) {
                    // Calculate the output section's bss size by using its non-bss end address and the current section's end address.
                    section_out.bss_size = cur_section.ram_addr + cur_section.size - output_section_bss_start;
                    input_section_to_output_section[section_index] = output_section_index;
                }
            }
            continue;
        }

        // Check if this section matches up with the previous section to merge them together.
        if (rom_to_ram == cur_rom_to_ram) {
            auto& section_out = ret.sections[output_section_index];
            uint32_t cur_section_end = cur_section.rom_addr + cur_section.size;
            section_out.size = cur_section_end - section_out.rom_addr;
        }
        // Otherwise, create a new output section and advance to it.
        else {
            output_section_index++;
            ret.sections.resize(output_section_index + 1);
            ret.section_functions.resize(output_section_index + 1);
            rom_to_ram = cur_rom_to_ram;

            auto& new_section = ret.sections[output_section_index];
            new_section.rom_addr = cur_section.rom_addr;
            new_section.ram_addr = cur_section.ram_addr;
            new_section.size = cur_section.size;
        }

        // Map this section to the current output section.
        input_section_to_output_section[section_index] = output_section_index;
        
        // Check for special section names.
        bool patch_section = cur_section.name == N64Recomp::PatchSectionName;
        bool force_patch_section = cur_section.name == N64Recomp::ForcedPatchSectionName;
        bool export_section = cur_section.name == N64Recomp::ExportSectionName;
        bool event_section = cur_section.name == N64Recomp::EventSectionName;
        bool import_section = cur_section.name.starts_with(N64Recomp::ImportSectionPrefix);
        bool callback_section = cur_section.name.starts_with(N64Recomp::CallbackSectionPrefix);

        // Add the functions from the current input section to the current output section.
        auto& section_out = ret.sections[output_section_index];
        
        const auto& cur_section_funcs = input_context.section_functions[section_index];


        // Skip the functions and relocs in this section if it's the event section, instead opting to create event functions from the section's functions.
        // This has to be done to find events that are never called, which may pop up as a valid use case for maintaining backwards compatibility
        // if a mod removes a call to an event but doesn't want to break mods that reference it. If this code wasn't present, then only events that are actually
        // triggered would show up in the mod's symbol file.
        if (event_section) {
            // Create event reference symbols for any functions in the event section. Ignore functions that already
            // have a symbol, since relocs from previous sections may have triggered creation of the event's reference symbol already.
            for (const auto& input_func_index : cur_section_funcs) {
                const auto& cur_func = input_context.functions[input_func_index];

                // Check if this event already has a symbol to prevent creating a duplicate.
                N64Recomp::SymbolReference event_ref;
                if (!ret.find_event_symbol(cur_func.name, event_ref)) {
                    ret.add_event_symbol(cur_func.name);
                }
            }
        }
        // Otherwise, copy the functions and relocs over from this section into the output context.
        // Import sections can be skipped, as those only contain dummy functions. Imports will be found while scanning relocs.
        else if (!import_section) {
            for (size_t section_function_index = 0; section_function_index < cur_section_funcs.size(); section_function_index++) {
                size_t output_func_index = ret.functions.size();
                size_t input_func_index = cur_section_funcs[section_function_index];
                const auto& cur_func = input_context.functions[input_func_index];

                // If this is the patch section, create a replacement for this function.
                if (patch_section || force_patch_section) {
                    // Find the corresponding symbol in the reference symbols.
                    N64Recomp::SymbolReference cur_reference;
                    bool original_func_exists = input_context.find_regular_reference_symbol(cur_func.name, cur_reference);

                    // Check that the function being patched exists in the original reference symbols.
                    if (!original_func_exists) {
                        fmt::print(stderr, "Function {} is marked as a patch but doesn't exist in the original ROM.\n", cur_func.name);
                        return {};
                    }

                    // Check that the reference symbol is actually a function.
                    const auto& reference_symbol = input_context.get_reference_symbol(cur_reference);
                    if (!reference_symbol.is_function) {
                        fmt::print(stderr, "Function {0} is marked as a patch, but {0} was a variable in the original ROM.\n", cur_func.name);
                        return {};
                    }

                    uint32_t reference_section_vram = input_context.get_reference_section_vram(reference_symbol.section_index);
                    uint32_t reference_section_rom = input_context.get_reference_section_rom(reference_symbol.section_index);

                    // Add a replacement for this function to the output context.
                    ret.replacements.emplace_back(
                        N64Recomp::FunctionReplacement {
                            .func_index = (uint32_t)output_func_index,
                            .original_section_vrom = reference_section_rom,
                            .original_vram = reference_section_vram + reference_symbol.section_offset,
                            .flags = force_patch_section ? N64Recomp::ReplacementFlags::Force : N64Recomp::ReplacementFlags{}
                        }
                    );
                }

                std::string name_out;

                if (export_section) {
                    ret.exported_funcs.push_back(output_func_index);
                    // Names are required for exported funcs, so copy the input function's name if we're in the export section.
                    name_out = cur_func.name;
                }

                if (callback_section) {
                    std::string dependency_name, event_name;
                    if (!parse_callback_name(std::string_view{ cur_section.name }.substr(N64Recomp::CallbackSectionPrefix.size()), dependency_name, event_name)) {
                        fmt::print(stderr, "Invalid mod name or event name for callback function {}.\n",
                            cur_func.name);
                        return {};
                    }

                    size_t dependency_index;
                    if (!ret.find_dependency(dependency_name, dependency_index)) {
                        fmt::print(stderr, "Failed to register callback {} to event {} from mod {} as the mod is not a registered dependency.\n",
                            cur_func.name, event_name, dependency_name);
                        return {};
                    }

                    size_t event_index;
                    if (!ret.add_dependency_event(event_name, dependency_index, event_index)) {
                        fmt::print(stderr, "Internal error: Failed to register event {} for dependency {}. Please report this issue.\n",
                            event_name, dependency_name);
                        return {};
                    }

                    if (!ret.add_callback(event_index, output_func_index)) {
                        fmt::print(stderr, "Internal error: Failed to add callback {} to event {} in dependency {}. Please report this issue.\n",
                            cur_func.name, event_name, dependency_name);
                        return {};
                    }
                }

                ret.section_functions[output_section_index].push_back(output_func_index);

                // Add this function to the output context.
                ret.functions.emplace_back(
                    cur_func.vram,
                    cur_func.rom,
                    std::vector<uint32_t>{}, // words
                    std::move(name_out), // name
                    (uint16_t)output_section_index,
                    false, // ignored
                    false, // reimplemented
                    false // stubbed
                );

                // Resize the words vector so the function has the correct size. No need to copy the words, as they aren't used when making a mod symbol file.
                ret.functions[output_func_index].words.resize(cur_func.words.size());
            }

            // Copy relocs and patch HI16/LO16/26 relocs for non-relocatable reference symbols
            section_out.relocs.reserve(section_out.relocs.size() + cur_section.relocs.size());
            for (const auto& cur_reloc : cur_section.relocs) {
                // Skip null relocs.
                if (cur_reloc.type == N64Recomp::RelocType::R_MIPS_NONE) {
                    continue;
                }
                // Reloc to a special section symbol.
                if (!input_context.is_regular_reference_section(cur_reloc.target_section)) {
                    section_out.relocs.emplace_back(cur_reloc);
                }
                // Reloc to a reference symbol.
                else if (cur_reloc.reference_symbol) {
                    bool is_relocatable = input_context.is_reference_section_relocatable(cur_reloc.target_section);
                    uint32_t section_vram = input_context.get_reference_section_vram(cur_reloc.target_section);
                    // Patch relocations to non-relocatable reference sections.
                    if (!is_relocatable) {
                        uint32_t reloc_target_address = section_vram + cur_reloc.target_section_offset;
                        uint32_t reloc_rom_address = cur_reloc.address - cur_section.ram_addr + cur_section.rom_addr;
                        
                        uint32_t* reloc_word_ptr = reinterpret_cast<uint32_t*>(ret.rom.data() + reloc_rom_address);
                        uint32_t reloc_word = byteswap(*reloc_word_ptr);
                        switch (cur_reloc.type) {
                            case N64Recomp::RelocType::R_MIPS_32:
                                // Don't patch MIPS32 relocations, as they've already been patched during elf parsing.
                                break;
                            case N64Recomp::RelocType::R_MIPS_26:
                                // Don't patch MIPS26 relocations, as there may be multiple functions with the same vram. Emit the reloc instead.
                                section_out.relocs.emplace_back(cur_reloc);
                                break;
                            case N64Recomp::RelocType::R_MIPS_NONE:
                                // Nothing to do.
                                break;
                            case N64Recomp::RelocType::R_MIPS_HI16:
                                reloc_word &= 0xFFFF0000;
                                reloc_word |= (reloc_target_address - (int16_t)(reloc_target_address & 0xFFFF)) >> 16 & 0xFFFF;
                                break;
                            case N64Recomp::RelocType::R_MIPS_LO16:
                                reloc_word &= 0xFFFF0000;
                                reloc_word |= reloc_target_address & 0xFFFF;
                                break;
                            default:
                                fmt::print(stderr, "Unsupported or unknown relocation type {} in reloc at address 0x{:08X} in section {}.\n",
                                    (int)cur_reloc.type, cur_reloc.address, cur_section.name);
                                return {};
                        }
                        *reloc_word_ptr = byteswap(reloc_word);
                    }
                    // Copy relocations to relocatable reference sections as-is.
                    else {
                        section_out.relocs.emplace_back(cur_reloc);
                    }
                }
                // Reloc to an internal symbol.
                else {
                    const N64Recomp::Section& target_section = input_context.sections[cur_reloc.target_section];
                    uint32_t output_section_offset = cur_reloc.target_section_offset + target_section.ram_addr - cur_section.ram_addr;

                    // Check if the target section is the event section. If so, create a reference symbol reloc
                    // to the event symbol, creating the event symbol if necessary.
                    if (target_section.name == N64Recomp::EventSectionName) {
                        if (cur_reloc.type != N64Recomp::RelocType::R_MIPS_26) {
                            fmt::print(stderr, "Symbol {} is an event and cannot have its address taken.\n",
                                cur_section.name);
                            return {};
                        }

                        uint32_t target_function_vram = cur_reloc.target_section_offset + target_section.ram_addr;
                        size_t target_function_index = input_context.find_function_by_vram_section(target_function_vram, cur_reloc.target_section);
                        if (target_function_index == (size_t)-1) {
                            fmt::print(stderr, "Internal error: Failed to find event symbol in section {} with offset 0x{:08X} (vram 0x{:08X}). Please report this issue.\n",
                                target_section.name, cur_reloc.target_section_offset, target_function_vram);
                            return {};
                        }

                        const auto& target_function = input_context.functions[target_function_index];

                        // Check if this event already has a symbol to prevent creating a duplicate.
                        N64Recomp::SymbolReference event_ref;
                        if (!ret.find_event_symbol(target_function.name, event_ref)) {
                            ret.add_event_symbol(target_function.name);
                            // Update the event symbol reference now that the symbol was created.
                            ret.find_event_symbol(target_function.name, event_ref);
                        }

                        // Create a reloc to the event symbol.
                        section_out.relocs.emplace_back(N64Recomp::Reloc{
                            .address = cur_reloc.address,
                            .target_section_offset = output_section_offset,
                            .symbol_index = static_cast<uint32_t>(event_ref.symbol_index),
                            .target_section = N64Recomp::SectionEvent,
                            .type = cur_reloc.type,
                            .reference_symbol = true,
                        });
                    }
                    // Check if the target is an import section. If so, create a reference symbol reloc
                    // to the import symbol, creating the import symbol if necessary.
                    else if (target_section.name.starts_with(N64Recomp::ImportSectionPrefix)) {
                        if (cur_reloc.type != N64Recomp::RelocType::R_MIPS_26) {
                            fmt::print(stderr, "Symbol {} is an import and cannot have its address taken.\n",
                                cur_section.name);
                            return {};
                        }

                        uint32_t target_function_vram = cur_reloc.target_section_offset + target_section.ram_addr;
                        size_t target_function_index = input_context.find_function_by_vram_section(target_function_vram, cur_reloc.target_section);
                        if (target_function_index == (size_t)-1) {
                            fmt::print(stderr, "Internal error: Failed to find import symbol in section {} with offset 0x{:08X} (vram 0x{:08X}). Please report this issue.\n",
                                target_section.name, cur_reloc.target_section_offset, target_function_vram);
                            return {};
                        }

                        const auto& target_function = input_context.functions[target_function_index];

                        // Find the dependency that this import belongs to.
                        std::string dependency_name = target_section.name.substr(N64Recomp::ImportSectionPrefix.size());
                        size_t dependency_index;
                        if (!ret.find_dependency(dependency_name, dependency_index)) {
                            fmt::print(stderr, "Failed to import function {} from mod {} as the mod is not a registered dependency.\n",
                                target_function.name, dependency_name);
                            return {};
                        }

                        // Check if this event already has a symbol to prevent creating a duplicate.
                        N64Recomp::SymbolReference import_ref;
                        if (!ret.find_import_symbol(target_function.name, dependency_index, import_ref)) {
                            ret.add_import_symbol(target_function.name, dependency_index);
                            // Update the event symbol reference now that the symbol was created.
                            ret.find_import_symbol(target_function.name, dependency_index, import_ref);
                        }

                        // Create a reloc to the event symbol.
                        section_out.relocs.emplace_back(N64Recomp::Reloc{
                            .address = cur_reloc.address,
                            .target_section_offset = output_section_offset,
                            .symbol_index = static_cast<uint32_t>(import_ref.symbol_index),
                            .target_section = N64Recomp::SectionImport,
                            .type = cur_reloc.type,
                            .reference_symbol = true,
                        });
                    }
                    // Not an import or event section, so handle the reloc normally.
                    else {
                        uint32_t target_rom_to_ram = target_section.ram_addr - target_section.rom_addr;
                        bool is_noload = target_section.rom_addr == (uint32_t)-1;
                        if (!is_noload && target_rom_to_ram != cur_rom_to_ram) {
                            fmt::print(stderr, "Reloc at address 0x{:08X} in section {} points to a different section.\n",
                                cur_reloc.address, cur_section.name);
                            return {};
                        }
                        section_out.relocs.emplace_back(N64Recomp::Reloc{
                            .address = cur_reloc.address,
                            // Use the original target section offset, this will be recalculated based on the output section afterwards.
                            .target_section_offset = cur_reloc.target_section_offset,
                            .symbol_index = 0,
                            // Use the input section index in the reloc, this will be converted to the output section afterwards.
                            .target_section = cur_reloc.target_section,
                            .type = cur_reloc.type,
                            .reference_symbol = false,
                        });
                    }
                }
            }
        }
    }

    // Fix up every internal reloc's target section based on the input to output section mapping.
    for (auto& section : ret.sections) {
        for (auto& reloc : section.relocs) {
            if (!reloc.reference_symbol) {
                uint16_t input_section_index = reloc.target_section;
                auto find_it = input_section_to_output_section.find(input_section_index);
                if (find_it == input_section_to_output_section.end()) {
                    fmt::print(stderr, "Reloc at address 0x{:08X} references section {}, which didn't get mapped to an output section\n",
                        reloc.address, input_context.sections[input_section_index].name);
                    return {};
                }
                uint16_t output_section_index = find_it->second;
                const auto& input_section = input_context.sections[input_section_index];
                const auto& output_section = ret.sections[output_section_index];
                // Adjust the reloc's target section offset based on the reloc's new section.
                reloc.target_section_offset = reloc.target_section_offset + input_section.ram_addr - output_section.ram_addr;
                // Replace the target section with the mapped output section.
                reloc.target_section = find_it->second;
            }
        }
    }

    // Copy the reference sections from the input context as-is for resolving reference symbol relocations.
    ret.copy_reference_sections_from(input_context);

    good = true;
    return ret;
}

bool create_mod_zip(const std::filesystem::path& output_dir, const ModConfig& config) {
    std::filesystem::path output_path = output_dir / (config.manifest.mod_id + "-" + config.manifest.version_string + ".nrm");

#ifdef _WIN32
    std::filesystem::path temp_zip_path = output_path;
    temp_zip_path.replace_extension(".zip");
    std::string command_string = fmt::format("powershell -command Compress-Archive -Force -CompressionLevel Optimal -DestinationPath \"{}\" -Path \"{}\",\"{}\",\"{}\"",
        temp_zip_path.string(), (output_dir / symbol_filename).string(), (output_dir / binary_filename).string(), (output_dir / manifest_filename).string());

    for (const auto& cur_file : config.inputs.additional_files) {
        command_string += fmt::format(",\"{}\"", cur_file.string());
    }

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    
    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );

    std::vector<char> command_string_buffer;
    command_string_buffer.resize(command_string.size() + 1);
    std::copy(command_string.begin(), command_string.end(), command_string_buffer.begin());
    command_string_buffer[command_string.size()] = '\x00';

    if (!CreateProcessA(NULL, command_string_buffer.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fmt::print(stderr, "Process creation failed {}\n", GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD ec;
    GetExitCodeProcess(pi.hProcess, &ec);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (ec != EXIT_SUCCESS) {
        fmt::print(stderr, "Compress-Archive failed with exit code {}\n", ec);
        return false;
    }

    std::error_code rename_ec;
    std::filesystem::rename(temp_zip_path, output_path, rename_ec);
    if (rename_ec != std::error_code{}) {
        fmt::print(stderr, "Failed to rename temporary zip to output path\n");
        return false;
    }
#else
    std::string args_string{};
    std::vector<size_t> arg_positions{};

    // Adds an argument with a null terminator to args_string, which is used as a buffer to hold null terminated arguments.
    // Also adds the argument's offset into the string into arg_positions for creating the array of character pointers for the exec.
    auto add_arg = [&args_string, &arg_positions](const std::string& arg){
        arg_positions.emplace_back(args_string.size());
        args_string += (arg + '\x00');
    };

    add_arg("zip"); // The program name (argv[0]).
    add_arg("-q"); // Quiet mode.
    add_arg("-9"); // Maximum compression level.
    add_arg("-MM"); // Error if any files aren't found.
    add_arg("-j"); // Junk the paths (store just as the provided filename).
    add_arg("-T"); // Test zip integrity.
    add_arg(output_path.string());
    add_arg((output_dir / symbol_filename).string());
    add_arg((output_dir / binary_filename).string());
    add_arg((output_dir / manifest_filename).string());

    // Add arguments for every additional file in the archive.
    for (const auto& cur_file : config.inputs.additional_files) {
        add_arg(cur_file.string());
    }

    // Build the argument char* array in a vector.
    std::vector<char*> arg_pointers{};
    for (size_t arg_index = 0; arg_index < arg_positions.size(); arg_index++) {
        arg_pointers.emplace_back(args_string.data() + arg_positions[arg_index]);
    }

    // Termimate the argument list with a null pointer.
    arg_pointers.emplace_back(nullptr);

    // Delete the output file if it exists already.
    std::filesystem::remove(output_path);

    // Fork-exec to run zip.
    pid_t pid = fork();
    if (pid == -1) {
        fmt::print(stderr, "Failed to run \"zip\"\n");
        return false;
    }
    else if (pid == 0) {
        // This is the child process, so exec zip with the arguments.
        if (execvp(arg_pointers[0], arg_pointers.data()) == -1) {
            fmt::print(stderr, "Failed to run \"zip\" ({})\n", errno);
        }
    }
    else {
        // This is the parent process, so wait for the child process to complete and check its exit code.
        int status;
        if (waitpid(pid, &status, 0) == (pid_t)-1) {
            fmt::print(stderr, "Waiting for \"zip\" failed\n");
            return false;
        }
        if (status != EXIT_SUCCESS) {
            fmt::print(stderr, "\"zip\" failed with exit code {}\n", status);
            return false;
        }
    }
#endif

    return true;
}

int main(int argc, const char** argv) {
    if (argc != 3) {
        fmt::print("Usage: {} [mod toml] [output folder]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    bool config_good;
    std::filesystem::path output_dir{ argv[2] };

    if (!std::filesystem::exists(output_dir)) {
        fmt::print(stderr, "Specified output folder does not exist!\n");
        return EXIT_FAILURE;
    }

    if (!std::filesystem::is_directory(output_dir)) {
        fmt::print(stderr, "Specified output folder is not a folder!\n");
        return EXIT_FAILURE;
    }

    ModConfig config = parse_mod_config(argv[1], config_good);

    if (!config_good) {
        fmt::print(stderr, "Failed to read mod config file: {}\n", argv[1]);
        return EXIT_FAILURE;
    }

    N64Recomp::Context context{};

    // Import symbols from symbols files that were provided.
    {
        // Create a new temporary context to read the function reference symbol file into, since it's the same format as the recompilation symbol file.
        std::vector<uint8_t> dummy_rom{};
        N64Recomp::Context reference_context{};
        if (!N64Recomp::Context::from_symbol_file(config.inputs.func_reference_syms_file_path, std::move(dummy_rom), reference_context, false)) {
            fmt::print(stderr, "Failed to load provided function reference symbol file\n");
            return EXIT_FAILURE;
        }

        // Use the reference context to build a reference symbol list for the actual context.
        if (!context.import_reference_context(reference_context)) {
            fmt::print(stderr, "Internal error: failed to import reference context. Please report this issue.\n");
            return EXIT_FAILURE;
        }
    }

    for (const std::filesystem::path& cur_data_sym_path : config.inputs.data_reference_syms_file_paths) {
        if (!context.read_data_reference_syms(cur_data_sym_path)) {
            fmt::print(stderr, "Failed to load provided data reference symbol file: {}\n", cur_data_sym_path.string());
            return EXIT_FAILURE;
        }
    }

    // Copy the dependencies from the config into the context.
    context.add_dependencies(config.manifest.dependencies);

    N64Recomp::ElfParsingConfig elf_config {
        .bss_section_suffix = {},
        .manually_sized_funcs = {},
        .relocatable_sections = {},
        .has_entrypoint = false,
        .entrypoint_address = 0,
        .use_absolute_symbols = false,
        .unpaired_lo16_warnings = false,
        .all_sections_relocatable = true
    };
    bool dummy_found_entrypoint;
    N64Recomp::DataSymbolMap dummy_syms_map;
    bool elf_good = N64Recomp::Context::from_elf_file(config.inputs.elf_path, context, elf_config, false, dummy_syms_map, dummy_found_entrypoint);

    if (!elf_good) {
        fmt::print(stderr, "Failed to parse mod elf\n");
        return EXIT_FAILURE;
    }

    if (context.sections.size() == 0) {
        fmt::print(stderr, "No sections found in mod elf\n");
        return EXIT_FAILURE;
    }

    bool mod_context_good;
    N64Recomp::Context mod_context = build_mod_context(context, mod_context_good);
    std::vector<uint8_t> symbols_bin = N64Recomp::symbols_to_bin_v1(mod_context);
    if (symbols_bin.empty()) {
        fmt::print(stderr, "Failed to create symbol file\n");
        return EXIT_FAILURE;
    }

    std::filesystem::path output_syms_path = output_dir / symbol_filename;
    std::filesystem::path output_binary_path = output_dir / binary_filename;
    std::filesystem::path output_manifest_path = output_dir / manifest_filename;

    // Write the symbol file.
    {
       std::ofstream output_syms_file{ output_syms_path, std::ios::binary };
       output_syms_file.write(reinterpret_cast<const char*>(symbols_bin.data()), symbols_bin.size());
    }

    // Write the binary file.
    {
       std::ofstream output_binary_file{ output_binary_path, std::ios::binary };
       output_binary_file.write(reinterpret_cast<const char*>(mod_context.rom.data()), mod_context.rom.size());
    }

    // Write the manifest.
    write_manifest(output_manifest_path, config.manifest);

    // Create the zip.
    if (!create_mod_zip(output_dir, config)) {
        fmt::print(stderr, "Failed to create mod file.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
