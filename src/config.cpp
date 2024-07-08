#include <iostream>

#include <toml++/toml.hpp>
#include "fmt/format.h"
#include "config.h"
#include "recomp_port.h"

std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
    if (!child.empty()) {
        return parent / child;
    }
    return child;
}

std::vector<RecompPort::ManualFunction> get_manual_funcs(const toml::array* manual_funcs_array) {
    std::vector<RecompPort::ManualFunction> ret;

    // Reserve room for all the funcs in the map.
    ret.reserve(manual_funcs_array->size());
    manual_funcs_array->for_each([&ret](auto&& el) {
        if constexpr (toml::is_table<decltype(el)>) {
            std::optional<std::string> func_name = el["name"].template value<std::string>();
            std::optional<std::string> section_name = el["section"].template value<std::string>();
            std::optional<uint32_t> vram_in = el["vram"].template value<uint32_t>();
            std::optional<uint32_t> size = el["size"].template value<uint32_t>();

            if (func_name.has_value() && section_name.has_value() && vram_in.has_value() && size.has_value()) {
                ret.emplace_back(func_name.value(), section_name.value(), vram_in.value(), size.value());
            } else {
                throw toml::parse_error("Missing required value in manual_funcs array", el.source());
            }
        }
        else {
            throw toml::parse_error("Missing required value in manual_funcs array", el.source());
        }
    });

    return ret;
}

std::vector<std::filesystem::path> get_data_syms_paths(const toml::array* data_syms_paths_array, const std::filesystem::path& basedir) {
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

std::vector<std::string> get_stubbed_funcs(const toml::table* patches_data) {
    std::vector<std::string> stubbed_funcs{};

    // Check if the stubs array exists.
    const toml::node_view stubs_data = (*patches_data)["stubs"];

    if (stubs_data.is_array()) {
        const toml::array* stubs_array = stubs_data.as_array();

        // Make room for all the stubs in the array.
        stubbed_funcs.reserve(stubs_array->size());

        // Gather the stubs and place them into the array.
        stubs_array->for_each([&stubbed_funcs](auto&& el) {
            if constexpr (toml::is_string<decltype(el)>) {
                stubbed_funcs.push_back(*el);
            }
            else {
                throw toml::parse_error("Invalid stubbed function", el.source());
            }
        });
    }

    return stubbed_funcs;
}

std::vector<std::string> get_ignored_funcs(const toml::table* patches_data) {
    std::vector<std::string> ignored_funcs{};

    // Check if the ignored funcs array exists.
    const toml::node_view ignored_funcs_data = (*patches_data)["ignored"];

    if (ignored_funcs_data.is_array()) {
        const toml::array* ignored_funcs_array = ignored_funcs_data.as_array();

        // Make room for all the ignored funcs in the array.
        ignored_funcs.reserve(ignored_funcs_array->size());

        // Gather the stubs and place them into the array.
        ignored_funcs_array->for_each([&ignored_funcs](auto&& el) {
            if constexpr (toml::is_string<decltype(el)>) {
                ignored_funcs.push_back(*el);
            }
        });
    }

    return ignored_funcs;
}

std::vector<RecompPort::FunctionSize> get_func_sizes(const toml::table* patches_data) {
    std::vector<RecompPort::FunctionSize> func_sizes{};

    // Check if the func size array exists.
    const toml::node_view funcs_data = (*patches_data)["function_sizes"];
    if (funcs_data.is_array()) {
        const toml::array* sizes_array = funcs_data.as_array();

        // Copy all the sizes into the output vector.
        sizes_array->for_each([&func_sizes](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                const toml::table& cur_size = *el.as_table();

                // Get the function name and size.
                std::optional<std::string> func_name = cur_size["name"].value<std::string>();
                std::optional<uint32_t> func_size = cur_size["size"].value<uint32_t>();

                if (func_name.has_value() && func_size.has_value()) {
                    // Make sure the size is divisible by 4
                    if (func_size.value() & (4 - 1)) {
                        // It's not, so throw an error (and make it look like a normal toml one).
                        throw toml::parse_error("Function size is not divisible by 4", el.source());
                    }
                }
                else {
                    throw toml::parse_error("Manually size function is missing required value(s)", el.source());
                }

                func_sizes.emplace_back(func_name.value(), func_size.value());
            }
            else {
                throw toml::parse_error("Invalid manually sized function entry", el.source());
            }
        });
    }

    return func_sizes;
}

std::vector<RecompPort::InstructionPatch> get_instruction_patches(const toml::table* patches_data) {
    std::vector<RecompPort::InstructionPatch> ret;

    // Check if the instruction patch array exists.
    const toml::node_view insn_patch_data = (*patches_data)["instruction"];

    if (insn_patch_data.is_array()) {
        const toml::array* insn_patch_array = insn_patch_data.as_array();
        ret.reserve(insn_patch_array->size());

        // Copy all the patches into the output vector.
        insn_patch_array->for_each([&ret](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                const toml::table& cur_patch = *el.as_table();

                // Get the vram and make sure it's 4-byte aligned.
                std::optional<uint32_t> vram = cur_patch["vram"].value<uint32_t>();
                std::optional<std::string> func_name = cur_patch["func"].value<std::string>();
                std::optional<uint32_t> value = cur_patch["value"].value<uint32_t>();

                if (!vram.has_value() || !func_name.has_value() || !value.has_value()) {
                    throw toml::parse_error("Instruction patch is missing required value(s)", el.source());
                }

                if (vram.value() & 0b11) {
                    // Not properly aligned, so throw an error (and make it look like a normal toml one).
                    throw toml::parse_error("Instruction patch is not word-aligned", el.source());
                }

                ret.push_back(RecompPort::InstructionPatch{
                    .func_name = func_name.value(),
                    .vram = (int32_t)vram.value(),
                    .value = value.value(),
                });
            }
            else {
                throw toml::parse_error("Invalid instruction patch entry", el.source());
            }
        });
    }

    return ret;
}

std::vector<RecompPort::FunctionHook> get_function_hooks(const toml::table* patches_data) {
    std::vector<RecompPort::FunctionHook> ret;

    // Check if the function hook array exists.
    const toml::node_view func_hook_data = (*patches_data)["hook"];

    if (func_hook_data.is_array()) {
        const toml::array* func_hook_array = func_hook_data.as_array();
        ret.reserve(func_hook_array->size());

        // Copy all the hooks into the output vector.
        func_hook_array->for_each([&ret](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                const toml::table& cur_hook = *el.as_table();

                // Get the vram and make sure it's 4-byte aligned.
                std::optional<uint32_t> before_vram = cur_hook["before_vram"].value<uint32_t>();
                std::optional<std::string> func_name = cur_hook["func"].value<std::string>();
                std::optional<std::string> text = cur_hook["text"].value<std::string>();

                if (!func_name.has_value() || !text.has_value()) {
                    throw toml::parse_error("Function hook is missing required value(s)", el.source());
                }

                if (before_vram.has_value() && before_vram.value() & 0b11) {
                    // Not properly aligned, so throw an error (and make it look like a normal toml one).
                    throw toml::parse_error("before_vram is not word-aligned", el.source());
                }

                ret.push_back(RecompPort::FunctionHook{
                    .func_name = func_name.value(),
                    .before_vram = before_vram.has_value() ? (int32_t)before_vram.value() : 0,
                    .text = text.value(),
                });
            }
            else {
                throw toml::parse_error("Invalid function hook entry", el.source());
            }
        });
    }

    return ret;
}

RecompPort::Config::Config(const char* path) {
    // Start this config out as bad so that it has to finish parsing without errors to be good.
    entrypoint = 0;
    bad = true;
    toml::table config_data{};

    try {
        config_data = toml::parse_file(path);
        std::filesystem::path basedir = std::filesystem::path{ path }.parent_path();

        // Input section (required)
        const auto input_data = config_data["input"];
        const auto entrypoint_data = input_data["entrypoint"];

        if (entrypoint_data) {
            const auto entrypoint_value = entrypoint_data.value<uint32_t>();
            if (entrypoint_value.has_value()) {
                entrypoint = (int32_t)entrypoint_value.value();
                has_entrypoint = true;
            }
            else {
                throw toml::parse_error("Invalid entrypoint", entrypoint_data.node()->source());
            }
        }
        else {
            has_entrypoint = false;
        }

        std::optional<std::string> elf_path_opt = input_data["elf_path"].value<std::string>();
        if (elf_path_opt.has_value()) {
            elf_path = concat_if_not_empty(basedir, elf_path_opt.value());
        }

        std::optional<std::string> symbols_file_path_opt = input_data["symbols_file_path"].value<std::string>();
        if (symbols_file_path_opt.has_value()) {
            symbols_file_path = concat_if_not_empty(basedir, symbols_file_path_opt.value());
        }

        std::optional<std::string> rom_file_path_opt = input_data["rom_file_path"].value<std::string>();
        if (rom_file_path_opt.has_value()) {
            rom_file_path = concat_if_not_empty(basedir, rom_file_path_opt.value());
        }

        std::optional<std::string> output_func_path_opt = input_data["output_func_path"].value<std::string>();
        if (output_func_path_opt.has_value()) {
            output_func_path = concat_if_not_empty(basedir, output_func_path_opt.value());
        }
        else {
            throw toml::parse_error("Missing output_func_path in config file", input_data.node()->source());
        }

        std::optional<std::string> relocatable_sections_path_opt = input_data["relocatable_sections_path"].value<std::string>();
        if (relocatable_sections_path_opt.has_value()) {
            relocatable_sections_path = concat_if_not_empty(basedir, relocatable_sections_path_opt.value());
        }
        else {
            relocatable_sections_path = "";
        }

        std::optional<bool> uses_mips3_float_mode_opt = input_data["uses_mips3_float_mode"].value<bool>();
        if (uses_mips3_float_mode_opt.has_value()) {
            uses_mips3_float_mode = uses_mips3_float_mode_opt.value();
        }
        else {
            uses_mips3_float_mode = false;
        }

        std::optional<std::string> bss_section_suffix_opt = input_data["bss_section_suffix"].value<std::string>();
        if (bss_section_suffix_opt.has_value()) {
            bss_section_suffix = bss_section_suffix_opt.value();
        }
        else {
            bss_section_suffix = ".bss";
        }

        std::optional<bool> single_file_output_opt = input_data["single_file_output"].value<bool>();
        if (single_file_output_opt.has_value()) {
            single_file_output = single_file_output_opt.value();
        }
        else {
            single_file_output = false;
        }

        std::optional<bool> use_absolute_symbols_opt = input_data["use_absolute_symbols"].value<bool>();
        if (use_absolute_symbols_opt.has_value()) {
            use_absolute_symbols = use_absolute_symbols_opt.value();
        }
        else {
            use_absolute_symbols = false;
        }

        // Manual functions (optional)
        toml::node_view manual_functions_data = input_data["manual_funcs"];
        if (manual_functions_data.is_array()) {
            const toml::array* array = manual_functions_data.as_array();
            manual_functions = get_manual_funcs(array);
        }

        // Output binary path when using an elf file input, includes patching reference symbol MIPS32 relocs (optional)
        std::optional<std::string> output_binary_path_opt = input_data["output_binary_path"].value<std::string>();
        if (output_binary_path_opt.has_value()) {
            output_binary_path = concat_if_not_empty(basedir, output_binary_path_opt.value());
        }
        else {
            output_binary_path = "";
        }

        // Control whether the recompiler warns about unpaired LO16 relocs (optional, defaults to true)
        std::optional<bool> unpaired_lo16_warnings_opt = input_data["unpaired_lo16_warnings"].value<bool>();
        if (unpaired_lo16_warnings_opt.has_value()) {
            unpaired_lo16_warnings = unpaired_lo16_warnings_opt.value();
        }
        else {
            unpaired_lo16_warnings = true;
        }

        std::optional<std::string> recomp_include_opt = input_data["recomp_include"].value<std::string>();
        if (recomp_include_opt.has_value()) {
            recomp_include = recomp_include_opt.value();
        }
        else {
            recomp_include = "#include \"librecomp/recomp.h\"";
        }

        std::optional<int32_t> funcs_per_file_opt = input_data["functions_per_output_file"].value<int32_t>();
        if (funcs_per_file_opt.has_value()) {
            functions_per_output_file = funcs_per_file_opt.value();
            if (functions_per_output_file <= 0) {
                throw toml::parse_error("Invalid functions_per_output_file value", input_data["functions_per_output_file"].node()->source());
            }
        }
        else {
            functions_per_output_file = 50;
        }

        // Patches section (optional)
        toml::node_view patches_data = config_data["patches"];
        if (patches_data.is_table()) {
            const toml::table* table = patches_data.as_table();

            // Stubs array (optional)
            stubbed_funcs = get_stubbed_funcs(table);

            // Ignored funcs array (optional)
            ignored_funcs = get_ignored_funcs(table);

            // Single-instruction patches (optional)
            instruction_patches = get_instruction_patches(table);

            // Manual function sizes (optional)
            manual_func_sizes = get_func_sizes(table);

            // Function hooks (optional)
            function_hooks = get_function_hooks(table);
        }

        // Function reference symbols file (optional)
        std::optional<std::string> func_reference_syms_file_opt = input_data["func_reference_syms_file"].value<std::string>();
        if (func_reference_syms_file_opt.has_value()) {
            if (!symbols_file_path.empty()) {
                throw toml::parse_error("Reference symbol files can only be used in elf input mode", input_data["func_reference_syms_file"].node()->source());
            }
            func_reference_syms_file_path = concat_if_not_empty(basedir, func_reference_syms_file_opt.value());
        }

        // Data reference symbols files (optional)
        toml::node_view data_reference_syms_file_data = input_data["data_reference_syms_files"];
        if (data_reference_syms_file_data.is_array()) {
            if (!symbols_file_path.empty()) {
                throw toml::parse_error("Reference symbol files can only be used in elf input mode", data_reference_syms_file_data.node()->source());
            }
            if (func_reference_syms_file_path.empty()) {
                throw toml::parse_error("Data reference symbol files can only be used if a function reference symbol file is also in use", data_reference_syms_file_data.node()->source());
            }
            const toml::array* array = data_reference_syms_file_data.as_array();
            data_reference_syms_file_paths = get_data_syms_paths(array, basedir);
        }
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return;
    }

    // No errors occured, so mark this config file as good.
    bad = false;
}

const std::unordered_map<std::string, RecompPort::RelocType> reloc_type_name_map {
    { "R_MIPS_NONE", RecompPort::RelocType::R_MIPS_NONE },
    { "R_MIPS_16", RecompPort::RelocType::R_MIPS_16 },
    { "R_MIPS_32", RecompPort::RelocType::R_MIPS_32 },
    { "R_MIPS_REL32", RecompPort::RelocType::R_MIPS_REL32 },
    { "R_MIPS_26", RecompPort::RelocType::R_MIPS_26 },
    { "R_MIPS_HI16", RecompPort::RelocType::R_MIPS_HI16 },
    { "R_MIPS_LO16", RecompPort::RelocType::R_MIPS_LO16 },
    { "R_MIPS_GPREL16", RecompPort::RelocType::R_MIPS_GPREL16 },
};

RecompPort::RelocType reloc_type_from_name(const std::string& reloc_type_name) {
    auto find_it = reloc_type_name_map.find(reloc_type_name);
    if (find_it != reloc_type_name_map.end()) {
        return find_it->second;
    }
    return RecompPort::RelocType::R_MIPS_NONE;
}

bool RecompPort::Context::from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, RecompPort::Context& out, bool with_relocs) {
    RecompPort::Context ret{};

    try {
        const toml::table config_data = toml::parse_file(symbol_file_path.u8string());
        const toml::node_view config_sections_value = config_data["section"];

        if (!config_sections_value.is_array()) {
            return false;
        }

        const toml::array* config_sections = config_sections_value.as_array();
        ret.section_functions.resize(config_sections->size());

        config_sections->for_each([&ret, &rom, with_relocs](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                std::optional<uint32_t> rom_addr = el["rom"].template value<uint32_t>();
                std::optional<uint32_t> vram_addr = el["vram"].template value<uint32_t>();
                std::optional<uint32_t> size = el["size"].template value<uint32_t>();
                std::optional<std::string> name = el["name"].template value<std::string>();

                if (!rom_addr.has_value() || !vram_addr.has_value() || !size.has_value() || !name.has_value()) {
                    throw toml::parse_error("Section entry missing required field(s)", el.source());
                }

                size_t section_index = ret.sections.size();

                Section& section = ret.sections.emplace_back(Section{});
                section.rom_addr = rom_addr.value();
                section.ram_addr = vram_addr.value();
                section.size = size.value();
                section.name = name.value();
                section.executable = true;

                // Read functions for the section.
                const toml::node_view cur_functions_value = el["functions"];
                if (!cur_functions_value.is_array()) {
                    throw toml::parse_error("Invalid functions array", cur_functions_value.node()->source());
                }

                const toml::array* cur_functions = cur_functions_value.as_array();
                cur_functions->for_each([&ret, &rom, &section, section_index](auto&& func_el) {
                    size_t function_index = ret.functions.size();

                    if constexpr (toml::is_table<decltype(func_el)>) {
                        std::optional<std::string> name = func_el["name"].template value<std::string>();
                        std::optional<uint32_t> vram_addr = func_el["vram"].template value<uint32_t>();
                        std::optional<uint32_t> func_size_ = func_el["size"].template value<uint32_t>();

                        if (!name.has_value() || !vram_addr.has_value() || !func_size_.has_value()) {
                            throw toml::parse_error("Function symbol entry is missing required field(s)", func_el.source());
                        }

                        uint32_t func_size = func_size_.value();

                        Function cur_func{};
                        cur_func.name = name.value();
                        cur_func.vram = vram_addr.value();
                        cur_func.rom = cur_func.vram - section.ram_addr + section.rom_addr;
                        cur_func.section_index = section_index;

                        if (cur_func.vram & 0b11) {
                            // Function isn't word aligned in vram.
                            throw toml::parse_error("Function's vram address isn't word aligned", func_el.source());
                        }

                        if (cur_func.rom & 0b11) {
                            // Function isn't word aligned in rom.
                            throw toml::parse_error("Function's rom address isn't word aligned", func_el.source());
                        }

                        // Read the function's words if a rom was provided.
                        if (!rom.empty()) {
                            if (cur_func.rom + func_size > rom.size()) {
                                // Function is out of bounds of the provided rom.
                                throw toml::parse_error("Function is out of bounds of the provided rom", func_el.source());
                            }

                            // Get the function's words from the rom.
                            cur_func.words.reserve(func_size / sizeof(uint32_t));
                            for (size_t rom_addr = cur_func.rom; rom_addr < cur_func.rom + func_size; rom_addr += sizeof(uint32_t)) {
                                cur_func.words.push_back(*reinterpret_cast<const uint32_t*>(rom.data() + rom_addr));
                            }
                        }

                        section.function_addrs.push_back(cur_func.vram);
                        ret.functions_by_name[cur_func.name] = function_index;
                        ret.functions_by_vram[cur_func.vram].push_back(function_index);
                        ret.section_functions[section_index].push_back(function_index);

                        ret.functions.emplace_back(std::move(cur_func));
                    }
                    else {
                        throw toml::parse_error("Invalid function symbol entry", func_el.source());
                    }
                });

                // Check if relocs exist for the section and read them if so.
                const toml::node_view relocs_value = el["relocs"];
                if (relocs_value.is_array()) {
                    // Mark the section as relocatable, since it has relocs.
                    section.relocatable = true;

                    if (with_relocs) {
                        // Read relocs for the section.
                        const toml::array* relocs_array = relocs_value.as_array();
                        relocs_array->for_each([&ret, &rom, &section, section_index](auto&& reloc_el) {
                            if constexpr (toml::is_table<decltype(reloc_el)>) {
                                std::optional<uint32_t> vram = reloc_el["vram"].template value<uint32_t>();
                                std::optional<uint32_t> target_vram = reloc_el["target_vram"].template value<uint32_t>();
                                std::optional<std::string> type_string = reloc_el["type"].template value<std::string>();

                                if (!vram.has_value() || !target_vram.has_value() || !type_string.has_value()) {
                                    throw toml::parse_error("Reloc entry missing required field(s)", reloc_el.source());
                                }

                                RelocType reloc_type = reloc_type_from_name(type_string.value());

                                if (reloc_type != RelocType::R_MIPS_HI16 && reloc_type != RelocType::R_MIPS_LO16 && reloc_type != RelocType::R_MIPS_32) {
                                    throw toml::parse_error("Invalid reloc entry type", reloc_el.source());
                                }

                                Reloc cur_reloc{};
                                cur_reloc.address = vram.value();
                                cur_reloc.section_offset = target_vram.value() - section.ram_addr;
                                cur_reloc.symbol_index = (uint32_t)-1;
                                cur_reloc.target_section = section_index;
                                cur_reloc.type = reloc_type;

                                section.relocs.emplace_back(cur_reloc);
                            }
                            else {
                                throw toml::parse_error("Invalid reloc entry", reloc_el.source());
                            }
                        });
                    }
                }
                else {
                    section.relocatable = false;
                }
            } else {
                throw toml::parse_error("Invalid section entry", el.source());
            }
        });
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return false;
    }

    ret.rom = std::move(rom);
    out = std::move(ret);
    return true;
}

void RecompPort::Context::import_reference_context(const RecompPort::Context& reference_context) {
    reference_sections.resize(reference_context.sections.size());
    reference_symbols.reserve(reference_context.functions.size());
    reference_symbol_names.reserve(reference_context.functions.size());

    // Copy the reference context's sections into the real context's reference sections. 
    for (size_t section_index = 0; section_index < reference_context.sections.size(); section_index++) {
        const RecompPort::Section& section_in = reference_context.sections[section_index];
        RecompPort::ReferenceSection& section_out = reference_sections[section_index];

        section_out.rom_addr = section_in.rom_addr;
        section_out.ram_addr = section_in.ram_addr;
        section_out.size = section_in.size;
        section_out.relocatable = section_in.relocatable;
    }

    // Copy the functions from the reference context into the reference context's function map.
    for (const RecompPort::Function& func_in: reference_context.functions) {
        const RecompPort::Section& func_section = reference_context.sections[func_in.section_index];

        reference_symbols_by_name.emplace(func_in.name, reference_symbols.size());

        reference_symbols.emplace_back(RecompPort::ReferenceSymbol{
            .section_index = func_in.section_index,
            .section_offset = func_in.vram - static_cast<uint32_t>(func_section.ram_addr),
            .is_function = true
        });
        reference_symbol_names.emplace_back(func_in.name);
    }
}

// Reads a data symbol file and adds its contents into this context's reference data symbols.
bool RecompPort::Context::read_data_reference_syms(const std::filesystem::path& data_syms_file_path) {
    try {
        const toml::table data_syms_file_data = toml::parse_file(data_syms_file_path.u8string());
        const toml::node_view data_sections_value = data_syms_file_data["section"];

        if (!data_sections_value.is_array()) {
            return false;
        }

        // Create a mapping of rom address to section to ensure that the same section indexes are used for both function and data reference symbols.
        std::unordered_map<uint32_t, uint16_t> ref_section_indices_by_vrom;

        for (uint16_t section_index = 0; section_index < reference_sections.size(); section_index++) {
            ref_section_indices_by_vrom.emplace(reference_sections[section_index].rom_addr, section_index);
        }
        
        const toml::array* data_sections = data_sections_value.as_array();
        
        data_sections->for_each([this, &ref_section_indices_by_vrom](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                std::optional<uint64_t> rom_addr = el["rom"].template value<uint64_t>();
                std::optional<uint32_t> vram_addr = el["vram"].template value<uint32_t>();
                std::optional<uint32_t> size = el["size"].template value<uint32_t>();
                std::optional<std::string> name = el["name"].template value<std::string>();

                if (!vram_addr.has_value() || !size.has_value() || !name.has_value()) {
                    throw toml::parse_error("Section entry missing required field(s)", el.source());
                }

                uint16_t ref_section_index;
                if (!rom_addr.has_value()) {
                    ref_section_index = RecompPort::SectionAbsolute; // Non-relocatable bss section or absolute symbols, mark this as an absolute symbol
                }
                else if (rom_addr.value() > 0xFFFFFFFF) {
                    throw toml::parse_error("Section has invalid ROM address", el.source());
                }
                else {
                    // Find the matching section from the function reference symbol file to ensure 
                    auto find_section_it = ref_section_indices_by_vrom.find(rom_addr.value());
                    if (find_section_it != ref_section_indices_by_vrom.end()) {
                        ref_section_index = find_section_it->second;
                    }
                    else {
                        ref_section_index = RecompPort::SectionAbsolute; // Not in the function symbol reference file, so this section can be treated as non-relocatable.
                    }
                }

                static ReferenceSection dummy_absolute_section {
                    .rom_addr = 0,
                    .ram_addr = 0,
                    .size = 0,
                    .relocatable = 0
                };
                const ReferenceSection& ref_section = ref_section_index == RecompPort::SectionAbsolute ? dummy_absolute_section : this->reference_sections[ref_section_index];

                // Sanity check this section against the matching one in the function reference symbol file if one exists.
                if (ref_section_index != RecompPort::SectionAbsolute) {
                    if (ref_section.ram_addr != vram_addr.value()) {
                        throw toml::parse_error("Section vram address differs from matching ROM address section in the function symbol reference file", el.source());
                    }

                    if (ref_section.size != size.value()) {
                        throw toml::parse_error("Section size address differs from matching ROM address section in the function symbol reference file", el.source());
                    }
                }

                // Read functions for the section.
                const toml::node_view cur_symbols_value = el["symbols"];
                if (!cur_symbols_value.is_array()) {
                    throw toml::parse_error("Invalid symbols array", cur_symbols_value.node()->source());
                }

                uint32_t ref_section_vram = ref_section.ram_addr;
                const toml::array* cur_symbols = cur_symbols_value.as_array();
                cur_symbols->for_each([this, ref_section_index, ref_section_vram](auto&& data_sym_el) {
                    
                    if constexpr (toml::is_table<decltype(data_sym_el)>) {
                        std::optional<std::string> name = data_sym_el["name"].template value<std::string>();
                        std::optional<uint32_t> vram_addr = data_sym_el["vram"].template value<uint32_t>();

                        if (!name.has_value() || !vram_addr.has_value()) {
                            throw toml::parse_error("Reference data symbol entry is missing required field(s)", data_sym_el.source());
                        }

                        this->reference_symbols_by_name.emplace(name.value(), reference_symbols.size());

                        this->reference_symbols.emplace_back(
                            ReferenceSymbol {
                                .section_index = ref_section_index,
                                .section_offset = vram_addr.value() - ref_section_vram,
                                .is_function = false
                            }
                        );
                        this->reference_symbol_names.emplace_back(name.value());
                    }
                    else {
                        throw toml::parse_error("Invalid data symbol entry", data_sym_el.source());
                    }
                });
            } else {
                throw toml::parse_error("Invalid section entry", el.source());
            }
        });
    }
    catch (const toml::parse_error& err) {
        std::cerr << "Syntax error parsing toml: " << *err.source().path << " (" << err.source().begin <<  "):\n" << err.description() << std::endl;
        return false;
    }

    return true;
}

