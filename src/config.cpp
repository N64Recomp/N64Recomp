#include <source_location>

#include "toml.hpp"
#include "fmt/format.h"
#include "recomp_port.h"

std::vector<std::string> get_stubbed_funcs(const toml::value& patches_data) {
	std::vector<std::string> stubbed_funcs{};

	// Check if the stubs array exists.
	const auto& stubs_data = toml::find_or<toml::value>(patches_data, "stubs", toml::value{});

	if (stubs_data.type() == toml::value_t::empty) {
		// No stubs, nothing to do here.
		return stubbed_funcs;
	}

	// Get the stubs array as an array type.
	const toml::array& stubs_array = stubs_data.as_array();

	// Make room for all the stubs in the array.
	stubbed_funcs.resize(stubs_array.size());

	// Gather the stubs and place them into the array.
	for (size_t stub_idx = 0; stub_idx < stubs_array.size(); stub_idx++) {
		// Copy the entry into the stubbed function list.
		stubbed_funcs[stub_idx] = stubs_array[stub_idx].as_string();
	}

	return stubbed_funcs;
}

std::unordered_map<std::string, RecompPort::FunctionArgType> arg_type_map{
	{"u32", RecompPort::FunctionArgType::u32},
	{"s32", RecompPort::FunctionArgType::s32},
};

std::vector<RecompPort::FunctionArgType> parse_args(const toml::array& args_in) {
	std::vector<RecompPort::FunctionArgType> ret(args_in.size());

	for (size_t arg_idx = 0; arg_idx < args_in.size(); arg_idx++) {
		const toml::value& arg_val = args_in[arg_idx];
		const std::string& arg_str = arg_val.as_string();

		// Check if the argument type string is valid.
		auto type_find = arg_type_map.find(arg_str);
		if (type_find == arg_type_map.end()) {
			// It's not, so throw an error (and make it look like a normal toml one).
			throw toml::type_error(toml::detail::format_underline(
				std::string{ std::source_location::current().function_name() } + ": invalid function arg type", {
					{arg_val.location(), ""}
				}), arg_val.location());
		}
		ret[arg_idx] = type_find->second;
	}

	return ret;
}

RecompPort::DeclaredFunctionMap get_declared_funcs(const toml::value& patches_data) {
	RecompPort::DeclaredFunctionMap declared_funcs{};

	// Check if the func array exists.
	const toml::value& funcs_data = toml::find_or<toml::value>(patches_data, "func", toml::value{});
	if (funcs_data.type() == toml::value_t::empty) {
		// No func array, nothing to do here
		return declared_funcs;
	}

	// Get the funcs array as an array type.
	const toml::array& funcs_array = funcs_data.as_array();

	// Reserve room for all the funcs in the map.
	declared_funcs.reserve(funcs_data.size());
	for (const toml::value& cur_func_val : funcs_array) {
		const std::string& func_name = toml::find<std::string>(cur_func_val, "name");
		const toml::array& args_in = toml::find<toml::array>(cur_func_val, "args");
		
		declared_funcs.emplace(func_name, parse_args(args_in));
	}

	return declared_funcs;
}

std::vector<RecompPort::InstructionPatch> get_instruction_patches(const toml::value& patches_data) {
	std::vector<RecompPort::InstructionPatch> ret;

	// Check if the instruction patch array exists.
	const toml::value& insn_patch_data = toml::find_or<toml::value>(patches_data, "instruction", toml::value{});
	if (insn_patch_data.type() == toml::value_t::empty) {
		// No instruction patch array, nothing to do here
		return ret;
	}

	// Get the instruction patch array as an array type.
	const toml::array& insn_patch_array = insn_patch_data.as_array();
	ret.resize(insn_patch_array.size());

	// Copy all the patches into the output vector.
	for (size_t patch_idx = 0; patch_idx < insn_patch_array.size(); patch_idx++) {
		const toml::value& cur_patch = insn_patch_array[patch_idx];

		ret[patch_idx].func_name = toml::find<std::string>(cur_patch, "func");
		ret[patch_idx].vram = toml::find<int32_t>(cur_patch, "vram");
		ret[patch_idx].value = toml::find<uint32_t>(cur_patch, "value");
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

	try {
		const toml::value config_data = toml::parse(path);
		std::filesystem::path basedir = std::filesystem::path{ path }.parent_path();

		// Input section (required)
		const toml::value& input_data = toml::find<toml::value>(config_data, "input");

		entrypoint = toml::find<int32_t>(input_data, "entrypoint");
		elf_path                  = concat_if_not_empty(basedir, toml::find<std::string>(input_data, "elf_path"));
		output_func_path          = concat_if_not_empty(basedir, toml::find<std::string>(input_data, "output_func_path"));
		relocatable_sections_path = concat_if_not_empty(basedir, toml::find_or<std::string>(input_data, "relocatable_sections_path", ""));

		// Patches section (optional)
		const toml::value& patches_data = toml::find_or<toml::value>(config_data, "patches", toml::value{});
		if (patches_data.type() != toml::value_t::empty) {
			// Stubs array (optional)
			stubbed_funcs = get_stubbed_funcs(patches_data);

			// Functions (optional)
			declared_funcs = get_declared_funcs(patches_data);

			// Single-instruction patches (optional)
			instruction_patches = get_instruction_patches(patches_data);
		}
	}
	catch (const toml::syntax_error& err) {
		fmt::print(stderr, "Syntax error in config file on line {}, full error:\n{}\n", err.location().line(), err.what());
		return;
	}
	catch (const toml::type_error& err) {
		fmt::print(stderr, "Incorrect type in config file on line {}, full error:\n{}\n", err.location().line(), err.what());
		return;
	}
	catch (const std::out_of_range& err) {
		fmt::print(stderr, "Missing value in config file, full error:\n{}\n", err.what());
		return;
	}

	// No errors occured, so mark this config file as good.
	bad = false;
}
