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
    std::vector<std::filesystem::path> dependency_paths;
};

static std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
    if (!child.empty()) {
        return parent / child;
    }
    return child;
}

static std::vector<std::filesystem::path> get_toml_path_array(const toml::array* toml_array, const std::filesystem::path& basedir) {
    std::vector<std::filesystem::path> ret;

    // Reserve room for all the funcs in the map.
    ret.reserve(toml_array->size());
    toml_array->for_each([&ret, &basedir](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
            ret.emplace_back(concat_if_not_empty(basedir, el.ref<std::string>()));
        }
        else {
            throw toml::parse_error("Invalid type for data reference symbol file entry", el.source());
        }
    });

    return ret;
}

static bool read_dependency_file(const std::filesystem::path& dependency_path, N64Recomp::Context& context, std::vector<std::string>& import_symbol_mod_ids) {
    toml::table toml_data{};

    try {
        toml_data = toml::parse_file(dependency_path.native());
        
        const auto dependency_data = toml_data["dependency"];
        if (!dependency_data.is_array()) {
            if (dependency_data) {
                throw toml::parse_error("No dependency array found", dependency_data.node()->source());
            }
            else {
                throw toml::parse_error("Invalid dependency array", dependency_data.node()->source());
            }
        }

        toml::array* dependency_array = dependency_data.as_array();
        for (const auto& dependency_node : *dependency_array) {
            if (!dependency_node.is_table()) {
                throw toml::parse_error("Invalid dependency entry", dependency_node.source());
            }

            // Mod ID
            toml::node_view mod_id_node = dependency_node[toml::path{"mod_id"}];
            if (!mod_id_node.is_string()) {
                if (mod_id_node) {
                    throw toml::parse_error("Invalid mod id", mod_id_node.node()->source());
                }
                else {
                    throw toml::parse_error("Dependency entry is missing mod id", dependency_node.source());
                }
            }
            const std::string& mod_id = mod_id_node.ref<std::string>();

            // Symbol list
            toml::node_view functions_data = dependency_node[toml::path{"functions"}];
            if (functions_data.is_array()) {
                const toml::array* functions_array = functions_data.as_array();
                for (const auto& function_node : *functions_array) {
                    if (!function_node.is_string()) {
                        throw toml::parse_error("Invalid dependency function", function_node.source());
                    }
                    const std::string& function_name = function_node.ref<std::string>();
                    context.reference_symbol_names.emplace_back(function_name);
                    context.reference_symbols_by_name[function_name] = context.reference_symbols.size();
                    context.reference_symbols.emplace_back(
                        N64Recomp::ReferenceSymbol {
                            .section_index = N64Recomp::SectionImport,
                            .section_offset = 0,
                            .is_function = true
                        }
                    );
                    import_symbol_mod_ids.emplace_back(mod_id);
                }
            }
            else {
                if (functions_data) {
                    throw toml::parse_error("Mod toml is missing data reference symbol file list", functions_data.node()->source());
                }
                else {
                    throw toml::parse_error("Invalid data reference symbol file list", functions_data.node()->source());
                }
            }
        }

    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing symbol import file: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return false;
    }

    return true;
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
            ret.data_reference_syms_file_paths = get_toml_path_array(array, basedir);
        }
        else {
            if (data_reference_syms_file_data) {
                throw toml::parse_error("Mod toml is missing data reference symbol file list", config_data.node()->source());
            }
            else {
                throw toml::parse_error("Invalid data reference symbol file list", data_reference_syms_file_data.node()->source());
            }
        }
        
        // Imported symbols files (optional)
        toml::node_view dependency_data = config_data["dependencies"];
        if (dependency_data.is_array()) {
            const toml::array* array = dependency_data.as_array();
            ret.dependency_paths = get_toml_path_array(array, basedir);
        }
        else if (dependency_data) {
            throw toml::parse_error("Invalid imported symbols file list", dependency_data.node()->source());
        }
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return {};
    }

    good = true;
    return ret;
}

static inline uint32_t round_up_16(uint32_t value) {
    return (value + 15) & (~15);
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
            const auto& section_a = input_context.sections[a];
            const auto& section_b = input_context.sections[b];
            // Sort primarily by ROM address.
            if (section_a.rom_addr != section_b.rom_addr) {
                return section_a.rom_addr < section_b.rom_addr;
            }
            // Sort secondarily by RAM address.
            return section_a.ram_addr < section_b.ram_addr;
        }
    );

    // TODO avoid a copy here.
    ret.base_context.rom = input_context.rom;

    uint32_t rom_to_ram = (uint32_t)-1;
    size_t output_section_index = (size_t)-1;
    ret.base_context.sections.resize(1);

    // Iterate over the input sections in their sorted order.
    for (uint16_t section_index : section_order) {
        const auto& cur_section = input_context.sections[section_index];
        uint32_t cur_rom_to_ram = cur_section.ram_addr - cur_section.rom_addr;

        // Check if this is a non-allocated section.
        if (cur_section.rom_addr == (uint32_t)-1) {
            // If so, check if it has a vram address directly after the current output section. If it does, then add this
            // section's size to the output section's bss size.
            if (output_section_index != -1 && cur_section.size != 0) {
                auto& section_out = ret.base_context.sections[output_section_index];
                uint32_t output_section_bss_start = section_out.ram_addr + section_out.size;
                uint32_t output_section_bss_end = output_section_bss_start + section_out.bss_size;
                // Check if the current section starts at the end of the output section, allowing for a range of matches to account for 16 byte section alignment.
                if (cur_section.ram_addr >= output_section_bss_end && cur_section.ram_addr <= round_up_16(output_section_bss_end)) {
                    // Calculate the output section's bss size by using its non-bss end address and the current section's end address.
                    section_out.bss_size = cur_section.ram_addr + cur_section.size - output_section_bss_start;
                }
            }
            continue;
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
                bool original_func_exists = false;
                auto find_sym_it = input_context.reference_symbols_by_name.find(cur_func.name);

                // Check if the function was found.
                if (find_sym_it == input_context.reference_symbols_by_name.end()) {
                    original_func_exists = false;
                }
                // Ignore reference symbols in the import section, as those are imports and not original symbols.
                else if (input_context.reference_symbols[find_sym_it->second].section_index == N64Recomp::SectionImport) {
                    original_func_exists = false;
                }
                else {
                    original_func_exists = true;
                }

                // Check that the function being patched exists in the original reference symbols.
                if (!original_func_exists) {
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

        // Copy relocs and patch HI16/LO16/26 relocs for non-relocatable reference symbols
        section_out.relocs.reserve(cur_section.relocs.size());
        for (const auto& cur_reloc : cur_section.relocs) {
            // Skip null relocs.
            if (cur_reloc.type == N64Recomp::RelocType::R_MIPS_NONE) {
                continue;
            }
            // Reloc to an imported symbol.
            if (cur_reloc.reference_symbol && cur_reloc.target_section == N64Recomp::SectionImport) {
                // Copy the reloc as-is.
                section_out.relocs.emplace_back(cur_reloc);
            }
            // Reloc to a reference symbol.
            else if (cur_reloc.reference_symbol) {
                const auto& reloc_section = input_context.reference_sections[cur_reloc.target_section];
                // Patch relocations to non-relocatable reference sections.
                if (!reloc_section.relocatable) {
                    uint32_t reloc_target_address = reloc_section.ram_addr + cur_reloc.target_section_offset;
                    uint32_t reloc_rom_address = cur_reloc.address - cur_section.ram_addr + cur_section.rom_addr;
                    
                    uint32_t* reloc_word_ptr = reinterpret_cast<uint32_t*>(ret.base_context.rom.data() + reloc_rom_address);
                    uint32_t reloc_word = byteswap(*reloc_word_ptr);
                    switch (cur_reloc.type) {
                        case N64Recomp::RelocType::R_MIPS_32:
                            // Don't patch MIPS32 relocations, as they've already been patched during elf parsing.
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
                        case N64Recomp::RelocType::R_MIPS_26:
                            if (reloc_target_address & 0x3) {
                                fmt::print("R_MIPS_26 reloc at address 0x{:08X} in section {} has a target address not divisible by 4!\n",
                                    cur_reloc.address, cur_section.name);
                                return {};
                            }
                            reloc_word &= 0xFC000000;
                            reloc_word |= (reloc_target_address >> 2) & 0x3FFFFFF;
                            break;
                        default:
                            fmt::print("Unsupported or unknown relocation type {} in reloc at address 0x{:08X} in section {}!\n",
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
                uint32_t target_rom_to_ram = target_section.ram_addr - target_section.rom_addr;
                bool is_noload = target_section.rom_addr == (uint32_t)-1;
                if (!is_noload && target_rom_to_ram != cur_rom_to_ram) {
                    fmt::print("Reloc at address 0x{:08X} in section {} points to a different section!\n",
                        cur_reloc.address, cur_section.name);
                    return {};
                }
                uint32_t output_section_offset = cur_reloc.target_section_offset + target_section.ram_addr - cur_section.ram_addr;
                section_out.relocs.emplace_back(N64Recomp::Reloc{
                    .address = cur_reloc.address,
                    .target_section_offset = output_section_offset,
                    .symbol_index = 0,
                    .target_section = N64Recomp::SectionSelf,
                    .type = cur_reloc.type,
                    .reference_symbol = false,
                });
            }
        }

        // TODO exports

        // TODO imports
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

    size_t import_section_symbol_start = context.reference_symbols.size();
    std::vector<std::string> import_symbol_mod_ids{};

    for (const std::filesystem::path& dependency_path : config.dependency_paths) {
        if (!read_dependency_file(dependency_path, context, import_symbol_mod_ids)) {
            fmt::print(stderr, "Failed to read dependency file: {}\n", dependency_path.string());
            return EXIT_FAILURE;
        }
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
    output_binary_file.write(reinterpret_cast<const char*>(mod_context.base_context.rom.data()), mod_context.base_context.rom.size());

    return EXIT_SUCCESS;
}
