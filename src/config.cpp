#include <source_location>

#include <toml++/toml.hpp>
#include "fmt/format.h"
#include "recomp_port.h"

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

std::unordered_map<std::string, RecompPort::FunctionArgType> arg_type_map{
	{"u32", RecompPort::FunctionArgType::u32},
	{"s32", RecompPort::FunctionArgType::s32},
};

std::vector<RecompPort::FunctionArgType> parse_args(const toml::array* args_in) {
	std::vector<RecompPort::FunctionArgType> ret(args_in->size());

    args_in->for_each([&ret](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
            const std::string& arg_str = *el;

            // Check if the argument type string is valid.
            auto type_find = arg_type_map.find(arg_str);
            if (type_find == arg_type_map.end()) {
                // It's not, so throw an error (and make it look like a normal toml one).
                throw toml::parse_error(("Invalid argument type: " + arg_str).c_str(), el.source());
            }
            ret.push_back(type_find->second);
        }
        else {
            throw toml::parse_error("Invalid function argument entry", el.source());
        }
    });

	return ret;
}

RecompPort::DeclaredFunctionMap get_declared_funcs(const toml::table* patches_data) {
	RecompPort::DeclaredFunctionMap declared_funcs{};

	// Check if the func array exists.
    const toml::node_view funcs_data = (*patches_data)["func"];

    if (funcs_data.is_array()) {
        const toml::array* funcs_array = funcs_data.as_array();

        // Reserve room for all the funcs in the map.
        declared_funcs.reserve(funcs_array->size());

        // Gather the funcs and place them into the map.
        funcs_array->for_each([&declared_funcs](auto&& el) {
            if constexpr (toml::is_table<decltype(el)>) {
                std::optional<std::string> func_name = el["name"].template value<std::string>();
                toml::node_view args_in = el["args"];

                if (func_name.has_value() && args_in.is_array()) {
                    const toml::array* args_array = args_in.as_array();
                    declared_funcs.emplace(func_name.value(), parse_args(args_array));
                } else {
                    throw toml::parse_error("Missing required value in func array", el.source());
                }
            }
            else {
                throw toml::parse_error("Invalid declared function entry", el.source());
            }
        });
    }

	return declared_funcs;
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

std::filesystem::path concat_if_not_empty(const std::filesystem::path& parent, const std::filesystem::path& child) {
	if (!child.empty()) {
		return parent / child;
	}
	return child;
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
            get_manual_funcs(array);
        }

        // Patches section (optional)
        toml::node_view patches_data = config_data["patches"];
        if (patches_data.is_table()) {
            const toml::table* table = patches_data.as_table();

            // Stubs array (optional)
            stubbed_funcs = get_stubbed_funcs(table);

            // Ignored funcs array (optional)
            ignored_funcs = get_ignored_funcs(table);

            // Functions (optional)
            declared_funcs = get_declared_funcs(table);

            // Single-instruction patches (optional)
            instruction_patches = get_instruction_patches(table);

            // Manual function sizes (optional)
            manual_func_sizes = get_func_sizes(table);
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

bool RecompPort::Context::from_symbol_file(const std::filesystem::path& symbol_file_path, std::vector<uint8_t>&& rom, RecompPort::Context& out) {
	RecompPort::Context ret{};

	try {
		const toml::table config_data = toml::parse_file(symbol_file_path.u8string());
        const toml::node_view config_sections_value = config_data["section"];

        if (!config_sections_value.is_array()) {
            return false;
        }

        const toml::array* config_sections = config_sections_value.as_array();
        ret.section_functions.resize(config_sections->size());

        config_sections->for_each([&ret, &rom](auto&& el) {
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

                        if (cur_func.rom + func_size > rom.size()) {
                            // Function is out of bounds of the provided rom.
                            throw toml::parse_error("Functio is out of bounds of the provided rom", func_el.source());
                        }

                        // Get the function's words from the rom.
                        cur_func.words.reserve(func_size / sizeof(uint32_t));
                        for (size_t rom_addr = cur_func.rom; rom_addr < cur_func.rom + func_size; rom_addr += sizeof(uint32_t)) {
                            cur_func.words.push_back(*reinterpret_cast<const uint32_t*>(rom.data() + rom_addr));
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

                            // TODO also accept MIPS32 for TLB relocations.
                            if (reloc_type != RelocType::R_MIPS_HI16 && reloc_type != RelocType::R_MIPS_LO16) {
                                throw toml::parse_error("Invalid reloc entry type", reloc_el.source());
                            }

                            Reloc cur_reloc{};
                            cur_reloc.address = vram.value();
                            cur_reloc.target_address = target_vram.value();
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
