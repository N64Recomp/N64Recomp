#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include "fmt/format.h"
#include "n64recomp.h"
#include <toml++/toml.hpp>

struct ModConfig {
    std::filesystem::path output_syms_path;
    std::filesystem::path output_binary_path;
    std::filesystem::path elf_path;
    std::filesystem::path func_reference_syms_file_path;
    std::vector<std::filesystem::path> data_reference_syms_file_paths;
};

static std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
    if (!child.empty()) {
        return parent / child;
    }
    return child;
}

static std::vector<std::filesystem::path> get_data_syms_paths(const toml::array* data_syms_paths_array, const std::filesystem::path& basedir) {
    std::vector<std::filesystem::path> ret;

    // Reserve room for all the funcs in the map.
    ret.reserve(data_syms_paths_array->size());
    data_syms_paths_array->for_each([&ret, &basedir](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
            ret.emplace_back(concat_if_not_empty(basedir, el.template value_exact<std::string>().value()));
        }
        else {
            throw toml::parse_error("Invalid type for data reference symbol file entry", el.source());
        }
    });

    return ret;
}

ModConfig parse_mod_config(const std::filesystem::path& config_path, bool& good) {
    ModConfig ret{};
    good = false;

    toml::table toml_data{};

    try {
        toml_data = toml::parse_file(config_path.native());
        std::filesystem::path basedir = config_path.parent_path();
        
        const auto config_data = toml_data["config"];

        // Output symbol file path
        std::optional<std::string> output_syms_path_opt = config_data["output_syms_path"].value<std::string>();
        if (output_syms_path_opt.has_value()) {
            ret.output_syms_path = concat_if_not_empty(basedir, output_syms_path_opt.value());
        }
        else {
            throw toml::parse_error("Mod toml is missing output symbol file path", config_data.node()->source());
        }

        // Output binary file path
        std::optional<std::string> output_binary_path_opt = config_data["output_binary_path"].value<std::string>();
        if (output_binary_path_opt.has_value()) {
            ret.output_binary_path = concat_if_not_empty(basedir, output_binary_path_opt.value());
        }
        else {
            throw toml::parse_error("Mod toml is missing output binary file path", config_data.node()->source());
        }

        // Elf file
        std::optional<std::string> elf_path_opt = config_data["elf_path"].value<std::string>();
        if (elf_path_opt.has_value()) {
            ret.elf_path = concat_if_not_empty(basedir, elf_path_opt.value());
        }
        else {
            throw toml::parse_error("Mod toml is missing elf file", config_data.node()->source());
        }
        
        // Function reference symbols file
        std::optional<std::string> func_reference_syms_file_opt = config_data["func_reference_syms_file"].value<std::string>();
        if (func_reference_syms_file_opt.has_value()) {
            ret.func_reference_syms_file_path = concat_if_not_empty(basedir, func_reference_syms_file_opt.value());
        }
        else {
            throw toml::parse_error("Mod toml is missing function reference symbol file", config_data.node()->source());
        }

        // Data reference symbols files
        toml::node_view data_reference_syms_file_data = config_data["data_reference_syms_files"];
        if (data_reference_syms_file_data.is_array()) {
            const toml::array* array = data_reference_syms_file_data.as_array();
            ret.data_reference_syms_file_paths = get_data_syms_paths(array, basedir);
        }
        else {
            if (data_reference_syms_file_data) {
                throw toml::parse_error("Mod toml is missing data reference symbol file list", config_data.node()->source());
            }
            else {
                throw toml::parse_error("Invalid data reference symbol file list", data_reference_syms_file_data.node()->source());
            }
        }
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return {};
    }

    good = true;
    return ret;
}

N64Recomp::ModContext build_mod_context(const N64Recomp::Context& input_context, bool& good) {
    N64Recomp::ModContext ret{};
    good = false;

    // Make a vector containing 0, 1, 2, ... section count - 1
    std::vector<uint16_t> section_order;
    section_order.resize(input_context.sections.size());
    std::iota(section_order.begin(), section_order.end(), 0);

    // Sort the vector based on the rom address of the corresponding section.
    std::sort(section_order.begin(), section_order.end(),
        [&](uint16_t a, uint16_t b) {
            return input_context.sections[a].rom_addr < input_context.sections[b].rom_addr;
        }
    );

    uint32_t rom_to_ram = (uint32_t)-1;
    size_t output_section_index = (size_t)-1;
    ret.base_context.sections.resize(1);

    // Iterate over the input sections in their sorted order.
    for (uint16_t section_index : section_order) {
        const auto& cur_section = input_context.sections[section_index];
        uint32_t cur_rom_to_ram = cur_section.ram_addr - cur_section.rom_addr;

        // Stop checking sections once a non-allocated section has been reached.
        if (cur_section.rom_addr == (uint32_t)-1) {
            break;
        }

        // Check if this section matches up with the previous section to merge them together.
        if (rom_to_ram == cur_rom_to_ram) {
            auto& section_out = ret.base_context.sections[output_section_index];
            uint32_t cur_section_end = cur_section.rom_addr + cur_section.size;
            section_out.size = cur_section_end - section_out.rom_addr;
        }
        // Otherwise, create a new output section and advance to it.
        else {
            output_section_index++;
            ret.base_context.sections.resize(output_section_index + 1);
            ret.base_context.section_functions.resize(output_section_index + 1);
            rom_to_ram = cur_rom_to_ram;

            auto& new_section = ret.base_context.sections[output_section_index];
            new_section.rom_addr = cur_section.rom_addr;
            new_section.ram_addr = cur_section.ram_addr;
            new_section.size = cur_section.size;
        }
        
        // Check for special section names.
        bool patch_section = cur_section.name == ".recomp_patch";
        bool force_patch_section = cur_section.name == ".recomp_force_patch";
        bool export_section = cur_section.name == ".recomp_export";

        // Add the functions from the current input section to the current output section.
        auto& section_out = ret.base_context.sections[output_section_index];

        size_t starting_function_index = ret.base_context.functions.size();
        const auto& cur_section_funcs = input_context.section_functions[section_index];

        for (size_t section_function_index = 0; section_function_index < cur_section_funcs.size(); section_function_index++) {
            size_t output_func_index = ret.base_context.functions.size();
            size_t input_func_index = cur_section_funcs[section_function_index];
            const auto& cur_func = input_context.functions[input_func_index];

            // If this is the patch section, create a replacement for this function.
            if (patch_section || force_patch_section) {
                // Find the corresponding symbol in the reference symbols.
                auto find_sym_it = input_context.reference_symbols_by_name.find(cur_func.name);
                if (find_sym_it == input_context.reference_symbols_by_name.end()) {
                    fmt::print("Function {} is marked as a patch but doesn't exist in the original ROM!\n", cur_func.name);
                    return {};
                }

                // Check that the reference symbol is actually a function.
                const auto& reference_symbol = input_context.reference_symbols[find_sym_it->second];
                if (!reference_symbol.is_function) {
                    fmt::print("Function {0} is marked as a patch, but {0} was a variable in the original ROM!\n", cur_func.name);
                    return {};
                }

                const auto& reference_section = input_context.reference_sections[reference_symbol.section_index];

                // Add a replacement for this function to the output context.
                ret.replacements.emplace_back(
                    N64Recomp::FunctionReplacement {
                        .func_index = (uint32_t)output_func_index,
                        .original_section_vrom = reference_section.rom_addr,
                        .original_vram = reference_section.ram_addr + reference_symbol.section_offset,
                        .flags = force_patch_section ? N64Recomp::ReplacementFlags::Force : N64Recomp::ReplacementFlags{}
                    }
                );
            }

            ret.base_context.section_functions[output_section_index].push_back(output_func_index);

            // Add this function to the output context.
            ret.base_context.functions.emplace_back(
                cur_func.vram,
                cur_func.rom,
                std::vector<uint32_t>{}, // words
                "", // name
                (uint16_t)output_section_index,
                false, // ignored
                false, // reimplemented
                false // stubbed
            );

            // Resize the words vector so the function has the correct size. No need to copy the words, as they aren't used when making a mod symbol file.
            ret.base_context.functions[output_func_index].words.resize(cur_func.words.size());
        }

        // TODO relocs (including reference symbols and HI16 and LO16 patching for non-relocatable reference symbols)


        // TODO exports
    }

    good = true;
    return ret;
}

int main(int argc, const char** argv) {
    if (argc != 2) {
        fmt::print("Usage: {} [mod toml]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    bool config_good;
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
        if (!N64Recomp::Context::from_symbol_file(config.func_reference_syms_file_path, std::move(dummy_rom), reference_context, false)) {
            fmt::print(stderr, "Failed to load provided function reference symbol file\n");
            return EXIT_FAILURE;
        }

        // Use the reference context to build a reference symbol list for the actual context.
        context.import_reference_context(reference_context);
    }

    for (const std::filesystem::path& cur_data_sym_path : config.data_reference_syms_file_paths) {
        if (!context.read_data_reference_syms(cur_data_sym_path)) {
            fmt::print(stderr, "Failed to load provided data reference symbol file: {}\n", cur_data_sym_path.string());
            return EXIT_FAILURE;
        }
    }

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
    bool elf_good = N64Recomp::Context::from_elf_file(config.elf_path, context, elf_config, false, dummy_syms_map, dummy_found_entrypoint);

    if (!elf_good) {
        fmt::print(stderr, "Failed to parse mod elf\n");
        return EXIT_FAILURE;
    }

    if (context.sections.size() == 0) {
        fmt::print(stderr, "No sections found in mod elf\n");
        return EXIT_FAILURE;
    }

    bool mod_context_good;
    N64Recomp::ModContext mod_context = build_mod_context(context, mod_context_good);
    std::vector<uint8_t> symbols_bin = N64Recomp::symbols_to_bin_v1(mod_context);

    std::ofstream output_syms_file{ config.output_syms_path, std::ios::binary };
    output_syms_file.write(reinterpret_cast<const char*>(symbols_bin.data()), symbols_bin.size());

    std::ofstream output_binary_file{ config.output_binary_path, std::ios::binary };
    output_binary_file.write(reinterpret_cast<const char*>(context.rom.data()), context.rom.size());

    return EXIT_SUCCESS;
}
