#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <span>
#include <filesystem>
#include <optional>

#include "rabbitizer.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recompiler/context.h"
#include "config.h"
#include <set>

void add_manual_functions(N64Recomp::Context& context, const std::vector<N64Recomp::ManualFunction>& manual_funcs) {
    auto exit_failure = [](const std::string& error_str) {
        fmt::vprint(stderr, error_str, fmt::make_format_args());
        std::exit(EXIT_FAILURE);
    };

    // Build a lookup from section name to section index.
    std::unordered_map<std::string, size_t> section_indices_by_name{};
    section_indices_by_name.reserve(context.sections.size());

    for (size_t i = 0; i < context.sections.size(); i++) {
        section_indices_by_name.emplace(context.sections[i].name, i);
    }

    for (const N64Recomp::ManualFunction& cur_func_def : manual_funcs) {
        const auto section_find_it = section_indices_by_name.find(cur_func_def.section_name);
        if (section_find_it == section_indices_by_name.end()) {
            exit_failure(fmt::format("Manual function {} specified with section {}, which doesn't exist!\n", cur_func_def.func_name, cur_func_def.section_name));
        }
        size_t section_index = section_find_it->second;

        const auto func_find_it = context.functions_by_name.find(cur_func_def.func_name);
        if (func_find_it != context.functions_by_name.end()) {
            exit_failure(fmt::format("Manual function {} already exists!\n", cur_func_def.func_name));
        }

        if ((cur_func_def.size & 0b11) != 0) {
            exit_failure(fmt::format("Manual function {} has a size that isn't divisible by 4!\n", cur_func_def.func_name));
        }

        auto& section = context.sections[section_index];
        uint32_t section_offset = cur_func_def.vram - section.ram_addr;
        uint32_t rom_address = section_offset + section.rom_addr;

        std::vector<uint32_t> words;
        words.resize(cur_func_def.size / 4);
        const uint32_t* elf_words = reinterpret_cast<const uint32_t*>(context.rom.data() + context.sections[section_index].rom_addr + section_offset);

        words.assign(elf_words, elf_words + words.size());

        size_t function_index = context.functions.size();
        context.functions.emplace_back(
            cur_func_def.vram,
            rom_address,
            std::move(words),
            cur_func_def.func_name,
            uint16_t(section_index),
            false,
            false,
            false
        );

        context.section_functions[section_index].push_back(function_index);
        section.function_addrs.push_back(function_index);
        context.functions_by_vram[cur_func_def.vram].push_back(function_index);
        context.functions_by_name[cur_func_def.func_name] = function_index;
    }
}

bool read_list_file(const std::filesystem::path& filename, std::vector<std::string>& entries_out) {
    std::ifstream input_file{ filename };
    if (!input_file.good()) {
        return false;
    }

    std::string entry;

    while (input_file >> entry) {
        entries_out.emplace_back(std::move(entry));
    }

    return true;
}

bool compare_files(const std::filesystem::path& file1_path, const std::filesystem::path& file2_path) {
    static std::vector<char> file1_buf(65536);
    static std::vector<char> file2_buf(65536);

    std::ifstream file1(file1_path, std::ifstream::ate | std::ifstream::binary); //open file at the end
    std::ifstream file2(file2_path, std::ifstream::ate | std::ifstream::binary); //open file at the end
    const std::ifstream::pos_type fileSize = file1.tellg();

    file1.rdbuf()->pubsetbuf(file1_buf.data(), file1_buf.size());
    file2.rdbuf()->pubsetbuf(file2_buf.data(), file2_buf.size());

    if (fileSize != file2.tellg()) {
        return false; //different file size
    }

    file1.seekg(0); //rewind
    file2.seekg(0); //rewind

    std::istreambuf_iterator<char> begin1(file1);
    std::istreambuf_iterator<char> begin2(file2);

    return std::equal(begin1, std::istreambuf_iterator<char>(), begin2); //Second argument is end-of-range iterator
}

bool recompile_single_function(const N64Recomp::Context& context, size_t func_index, const std::string& recomp_include, const std::filesystem::path& output_path, std::span<std::vector<uint32_t>> static_funcs_out) {
    // Open the temporary output file
    std::filesystem::path temp_path = output_path;
    temp_path.replace_extension(".tmp");
    std::ofstream output_file{ temp_path };
    if (!output_file.good()) {
        fmt::print(stderr, "Failed to open file for writing: {}\n", temp_path.string() );
        return false;
    }

    // Write the file header
    fmt::print(output_file,
        "{}\n"
        "\n",
        recomp_include);

    if (!N64Recomp::recompile_function(context, func_index, output_file, static_funcs_out, false)) {
        return false;
    }
    
    output_file.close();

    // If a file of the target name exists and it's identical to the output file, delete the output file.
    // This prevents updating the existing file so that it doesn't need to be rebuilt.
    if (std::filesystem::exists(output_path) && compare_files(output_path, temp_path)) {
        std::filesystem::remove(temp_path);
    }
    // Otherwise, rename the new file to the target path.
    else {
        std::filesystem::rename(temp_path, output_path);
    }

    return true;
}

std::vector<std::string> reloc_names {
    "R_MIPS_NONE ",
    "R_MIPS_16",
    "R_MIPS_32",
    "R_MIPS_REL32",
    "R_MIPS_26",
    "R_MIPS_HI16",
    "R_MIPS_LO16",
    "R_MIPS_GPREL16",
};

void dump_context(const N64Recomp::Context& context, const std::unordered_map<uint16_t, std::vector<N64Recomp::DataSymbol>>& data_syms, const std::filesystem::path& func_path, const std::filesystem::path& data_path) {
    std::ofstream func_context_file {func_path};
    std::ofstream data_context_file {data_path};
    
    fmt::print(func_context_file, "# Autogenerated from an ELF via N64Recomp\n");
    fmt::print(data_context_file, "# Autogenerated from an ELF via N64Recomp\n");

    auto print_section = [](std::ofstream& output_file, const std::string& name, uint32_t rom_addr, uint32_t ram_addr, uint32_t size) {
        if (rom_addr == (uint32_t)-1) {
            fmt::print(output_file,
                "[[section]]\n"
                "name = \"{}\"\n"
                "vram = 0x{:08X}\n"
                "size = 0x{:X}\n"
                "\n",
                name, ram_addr, size);
        }
        else {
            fmt::print(output_file,
                "[[section]]\n"
                "name = \"{}\"\n"
                "rom = 0x{:08X}\n"
                "vram = 0x{:08X}\n"
                "size = 0x{:X}\n"
                "\n",
                name, rom_addr, ram_addr, size);
        }
    };

    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        const N64Recomp::Section& section = context.sections[section_index];
        const std::vector<size_t>& section_funcs = context.section_functions[section_index];
        if (!section_funcs.empty()) {
            print_section(func_context_file, section.name, section.rom_addr, section.ram_addr, section.size);

            // Dump relocs into the function context file.
            if (!section.relocs.empty()) {
                fmt::print(func_context_file, "relocs = [\n");

                for (const N64Recomp::Reloc& reloc : section.relocs) {
                    if (reloc.target_section == section_index || reloc.target_section == section.bss_section_index) {
                        // TODO allow emitting MIPS32 relocs for specific sections via a toml option for TLB mapping support.
                        if (reloc.type == N64Recomp::RelocType::R_MIPS_HI16 || reloc.type == N64Recomp::RelocType::R_MIPS_LO16 || reloc.type == N64Recomp::RelocType::R_MIPS_26) {
                            fmt::print(func_context_file, "    {{ type = \"{}\", vram = 0x{:08X}, target_vram = 0x{:08X} }},\n",
                                reloc_names[static_cast<int>(reloc.type)], reloc.address, reloc.target_section_offset + section.ram_addr);
                        }
                    }
                }

                fmt::print(func_context_file, "]\n\n");
            }

            // Dump functions into the function context file.
            fmt::print(func_context_file, "functions = [\n");

            for (const size_t& function_index : section_funcs) {
                const N64Recomp::Function& func = context.functions[function_index];
                fmt::print(func_context_file, "    {{ name = \"{}\", vram = 0x{:08X}, size = 0x{:X} }},\n",
                    func.name, func.vram, func.words.size() * sizeof(func.words[0]));
            }

            fmt::print(func_context_file, "]\n\n");
        }
        
        const auto find_syms_it = data_syms.find((uint16_t)section_index);
        if (find_syms_it != data_syms.end() && !find_syms_it->second.empty()) {
            print_section(data_context_file, section.name, section.rom_addr, section.ram_addr, section.size);

            // Dump other symbols into the data context file.
            fmt::print(data_context_file, "symbols = [\n");

            for (const N64Recomp::DataSymbol& cur_sym : find_syms_it->second) {
                fmt::print(data_context_file, "    {{ name = \"{}\", vram = 0x{:08X} }},\n", cur_sym.name, cur_sym.vram);
            }
            
            fmt::print(data_context_file, "]\n\n");
        }
    }

    const auto find_abs_syms_it = data_syms.find(N64Recomp::SectionAbsolute);
    if (find_abs_syms_it != data_syms.end() && !find_abs_syms_it->second.empty()) {
        // Dump absolute symbols into the data context file.
        print_section(data_context_file, "ABSOLUTE_SYMS", (uint32_t)-1, 0, 0);
        fmt::print(data_context_file, "symbols = [\n");

        for (const N64Recomp::DataSymbol& cur_sym : find_abs_syms_it->second) {
            fmt::print(data_context_file, "    {{ name = \"{}\", vram = 0x{:08X} }},\n", cur_sym.name, cur_sym.vram);
        }

        fmt::print(data_context_file, "]\n\n");
    }
}

static std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::vector<uint8_t> ret;

    std::ifstream file{ path, std::ios::binary};

    if (file.good()) {
        file.seekg(0, std::ios::end);
        ret.resize(file.tellg());
        file.seekg(0, std::ios::beg);

        file.read(reinterpret_cast<char*>(ret.data()), ret.size());
    }

    return ret;
}

int main(int argc, char** argv) {
    auto exit_failure = [] (const std::string& error_str) {
        fmt::vprint(stderr, error_str, fmt::make_format_args());
        std::exit(EXIT_FAILURE);
    };

    bool dumping_context = false;

    if (argc < 2) {
        fmt::print("Usage: {} <config file> [--dump-context]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    const char* config_path = argv[1];

    for (size_t i = 2; i < argc; i++) {
        std::string_view cur_arg = argv[i];
        if (cur_arg == "--dump-context") {
            dumping_context = true;
        }
        else {
            fmt::print("Unknown argument \"{}\"\n", cur_arg);
            return EXIT_FAILURE;
        }
    }

    N64Recomp::Config config{ config_path };
    if (!config.good()) {
        exit_failure(fmt::format("Failed to load config file: {}\n", config_path));
    }

    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;

    std::vector<std::string> relocatable_sections_ordered{};

    if (!config.relocatable_sections_path.empty()) {
        if (!read_list_file(config.relocatable_sections_path, relocatable_sections_ordered)) {
            exit_failure(fmt::format("Failed to load the relocatable section list file: {}\n", (const char*)config.relocatable_sections_path.u8string().c_str()));
        }
    }

    std::unordered_set<std::string> relocatable_sections{};
    relocatable_sections.insert(relocatable_sections_ordered.begin(), relocatable_sections_ordered.end());

    std::unordered_set<std::string> ignored_syms_set{};
    ignored_syms_set.insert(config.ignored_funcs.begin(), config.ignored_funcs.end());

    N64Recomp::Context context{};
    
    if (!config.elf_path.empty() && !config.symbols_file_path.empty()) {
        exit_failure("Config file cannot provide both an elf and a symbols file\n");
    }

    // Build a context from the provided elf file.
    if (!config.elf_path.empty()) {
        // Lists of data symbols organized by section, only used if dumping context.
        std::unordered_map<uint16_t, std::vector<N64Recomp::DataSymbol>> data_syms;

        // Import symbols from any reference symbols files that were provided.
        if (!config.func_reference_syms_file_path.empty()) {
            {
                // Create a new temporary context to read the function reference symbol file into, since it's the same format as the recompilation symbol file.
                std::vector<uint8_t> dummy_rom{};
                N64Recomp::Context reference_context{};
                if (!N64Recomp::Context::from_symbol_file(config.func_reference_syms_file_path, std::move(dummy_rom), reference_context, false)) {
                    exit_failure("Failed to load provided function reference symbol file\n");
                }

                // Use the reference context to build a reference symbol list for the actual context.
                if (!context.import_reference_context(reference_context)) {
                    exit_failure("Internal error: Failed to import reference context. Please report this issue.\n");
                }
            }

            for (const std::filesystem::path& cur_data_sym_path : config.data_reference_syms_file_paths) {
                if (!context.read_data_reference_syms(cur_data_sym_path)) {
                    exit_failure(fmt::format("Failed to load provided data reference symbol file: {}\n", cur_data_sym_path.string()));
                }
            }
        }

        N64Recomp::ElfParsingConfig elf_config {
            .bss_section_suffix = config.bss_section_suffix,
            .relocatable_sections = std::move(relocatable_sections),
            .ignored_syms = std::move(ignored_syms_set),
            .mdebug_text_map = config.mdebug_text_map,
            .mdebug_data_map = config.mdebug_data_map,
            .mdebug_rodata_map = config.mdebug_rodata_map,
            .mdebug_bss_map = config.mdebug_bss_map,
            .has_entrypoint = config.has_entrypoint,
            .entrypoint_address = config.entrypoint,
            .use_absolute_symbols = config.use_absolute_symbols,
            .unpaired_lo16_warnings = config.unpaired_lo16_warnings,
            .all_sections_relocatable = false,
            .use_mdebug = config.use_mdebug,
        };

        for (const auto& func_size : config.manual_func_sizes) {
            elf_config.manually_sized_funcs.emplace(func_size.func_name, func_size.size_bytes);
        }

        bool found_entrypoint_func;
        if (!N64Recomp::Context::from_elf_file(config.elf_path, context, elf_config, dumping_context, data_syms, found_entrypoint_func)) {
            exit_failure("Failed to parse elf\n");
        }

        // Add any manual functions
        add_manual_functions(context, config.manual_functions);

        if (config.has_entrypoint && !found_entrypoint_func) {
            exit_failure("Could not find entrypoint function\n");
        }
        
        if (dumping_context) {
            fmt::print("Dumping context\n");
            // Sort the data syms by address so the output is nicer.
            for (auto& [section_index, section_syms] : data_syms) {
                std::sort(section_syms.begin(), section_syms.end(),
                    [](const N64Recomp::DataSymbol& a, const N64Recomp::DataSymbol& b) {
                        return a.vram < b.vram;
                    }
                );
            }

            dump_context(context, data_syms, "dump.toml", "data_dump.toml");
            return 0;
        }
    }
    // Build a context from the provided symbols file.
    else if (!config.symbols_file_path.empty()) {
        if (config.rom_file_path.empty()) {
            exit_failure("A ROM file must be provided when using a symbols file\n");
        }

        if (dumping_context) {
            exit_failure("Cannot dump context when using a symbols file\n");
        }

        std::vector<uint8_t> rom = read_file(config.rom_file_path);
        if (rom.empty()) {
            exit_failure("Failed to load ROM file: " + config.rom_file_path.string() + "\n");
        }
        
        if (!N64Recomp::Context::from_symbol_file(config.symbols_file_path, std::move(rom), context, true)) {
            exit_failure("Failed to load symbols file\n");
        }

        auto rename_function = [&context](size_t func_index, const std::string& new_name) {
            N64Recomp::Function& func = context.functions[func_index];

            context.functions_by_name.erase(func.name);
            func.name = new_name;
            context.functions_by_name[func.name] = func_index;
        };

        for (size_t func_index = 0; func_index < context.functions.size(); func_index++) {
            N64Recomp::Function& func = context.functions[func_index];
            if (N64Recomp::reimplemented_funcs.contains(func.name)) {
                rename_function(func_index, func.name + "_recomp");
                func.reimplemented = true;
                func.ignored = true;
            } else if (N64Recomp::ignored_funcs.contains(func.name)) {
                rename_function(func_index, func.name + "_recomp");
                func.ignored = true;
            } else if (N64Recomp::renamed_funcs.contains(func.name)) {
                rename_function(func_index, func.name + "_recomp");
                func.ignored = false;
            }
        }


        if (config.has_entrypoint) {
            bool found_entrypoint = false;

            for (uint32_t func_index : context.functions_by_vram[config.entrypoint]) {
                auto& func = context.functions[func_index];
                if (func.rom == 0x1000) {
                    rename_function(func_index, "recomp_entrypoint");
                    found_entrypoint = true;
                    break;
                }
            }

            if (!found_entrypoint) {
                exit_failure("No entrypoint provided in symbol file\n");
            }
        }

    }
    else {
        exit_failure("Config file must provide either an elf or a symbols file\n");
    }


    fmt::print("Function count: {}\n", context.functions.size());

    std::filesystem::create_directories(config.output_func_path);

    std::ofstream func_header_file{ config.output_func_path / "funcs.h" };

    fmt::print(func_header_file,
        "{}\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {{\n"
        "#endif\n"
        "\n",
        config.recomp_include
    );

    std::vector<std::vector<uint32_t>> static_funcs_by_section{ context.sections.size() };

    fmt::print("Working dir: {}\n", std::filesystem::current_path().string());

    // Stub out any functions specified in the config file.
    for (const std::string& stubbed_func : config.stubbed_funcs) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(stubbed_func);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to stub it out.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} is stubbed out in the config file but does not exist!", stubbed_func));
        }
        // Mark the function as stubbed.
        context.functions[func_find->second].stubbed = true;
    }

    // Ignore any functions specified in the config file.
    for (const std::string& ignored_func : config.ignored_funcs) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(ignored_func);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to mark it as ignored.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} is set as ignored in the config file but does not exist!", ignored_func));
        }
        // Mark the function as ignored.
        context.functions[func_find->second].ignored = true;
    }

    // Rename any functions specified in the config file.
    for (const std::string& renamed_func : config.renamed_funcs) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(renamed_func);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to rename it.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} is set as renamed in the config file but does not exist!", renamed_func));
        }
        // Rename the function.
        N64Recomp::Function* func = &context.functions[func_find->second];
        func->name = func->name + "_recomp";
    }

    // Propogate the trace mode parameter.
    context.trace_mode = config.trace_mode;

    // Apply any single-instruction patches.
    for (const N64Recomp::InstructionPatch& patch : config.instruction_patches) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(patch.func_name);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to stub it out.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} has an instruction patch but does not exist!", patch.func_name));
        }

        N64Recomp::Function& func = context.functions[func_find->second];
        int32_t func_vram = func.vram;

        // Check that the function actually contains this vram address.
        if (patch.vram < func_vram || patch.vram >= func_vram + func.words.size() * sizeof(func.words[0])) {
            exit_failure(fmt::format("Function {} has an instruction patch for vram 0x{:08X} but doesn't contain that vram address!", patch.func_name, (uint32_t)patch.vram));
        }

        // Calculate the instruction index and modify the instruction.
        size_t instruction_index = (static_cast<size_t>(patch.vram) - func_vram) / sizeof(uint32_t);
        func.words[instruction_index] = byteswap(patch.value);
    }

    // Apply any function hooks.
    for (const N64Recomp::FunctionTextHook& patch : config.function_hooks) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(patch.func_name);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to stub it out.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} has a function hook but does not exist!", patch.func_name));
        }

        N64Recomp::Function& func = context.functions[func_find->second];
        int32_t func_vram = func.vram;

        // Check that the function actually contains this vram address.
        if (patch.before_vram < func_vram || patch.before_vram >= func_vram + func.words.size() * sizeof(func.words[0])) {
            exit_failure(fmt::format("Function {} has a function hook for vram 0x{:08X} but doesn't contain that vram address!", patch.func_name, (uint32_t)patch.before_vram));
        }

        // No after_vram means this will be placed at the start of the function
        size_t instruction_index = -1;

        // Calculate the instruction index.
        if (patch.before_vram != 0) {
          instruction_index = (static_cast<size_t>(patch.before_vram) - func_vram) / sizeof(uint32_t);
        }

        // Check if a function hook already exits for that instruction index.
        auto hook_find = func.function_hooks.find(instruction_index);
        if (hook_find != func.function_hooks.end()) {
            exit_failure(fmt::format("Function {} already has a function hook for vram 0x{:08X}!", patch.func_name, (uint32_t)patch.before_vram));
        }

        func.function_hooks[instruction_index] = patch.text;
    }

    // Apply function hook definitions from config
    for (const N64Recomp::FunctionHookDefinition& hook_def : config.function_hook_definitions) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(hook_def.func_name);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing.
            exit_failure(fmt::format("Function {} has a hook definition but does not exist!", hook_def.func_name));
        }

        N64Recomp::Function& func = context.functions[func_find->second];
        int32_t func_vram = func.vram;

        int32_t hook_vram = 0;
        
        if (hook_def.before_call) {
            // Hook at the beginning of the function (before first instruction)
            hook_vram = -1;
        } else {
            // Hook at specific vram address
            hook_vram = hook_def.before_vram;
            
            // Check that the function actually contains this vram address.
            if (hook_vram < func_vram || hook_vram >= func_vram + func.words.size() * sizeof(func.words[0])) {
                exit_failure(fmt::format("Function {} has a hook definition for vram 0x{:08X} but doesn't contain that vram address!", 
                    hook_def.func_name, (uint32_t)hook_vram));
            }
        }

        // Calculate the instruction index.
        size_t instruction_index = -1;
        if (hook_vram != -1) {
            instruction_index = (static_cast<size_t>(hook_vram) - func_vram) / sizeof(uint32_t);
        }

        // Check if a function hook already exists for that instruction index.
        auto hook_find = func.function_hooks.find(instruction_index);
        if (hook_find != func.function_hooks.end()) {
            exit_failure(fmt::format("Function {} already has a function hook for vram 0x{:08X}!", 
                hook_def.func_name, hook_vram == -1 ? func_vram : (uint32_t)hook_vram));
        }

        // Generate the hook call text
        std::string hook_text = fmt::format("{}(rdram, ctx);", hook_def.hook_func_name);
        func.function_hooks[instruction_index] = hook_text;
        
        // Add the hook function to the header file declarations
        fmt::print(func_header_file, "void {}(uint8_t* rdram, recomp_context* ctx);\n", hook_def.hook_func_name);
    }

    std::ofstream current_output_file;
    size_t output_file_count = 0;
    size_t cur_file_function_count = 0;
    
    auto open_new_output_file = [&config, &current_output_file, &output_file_count, &cur_file_function_count]() {
        current_output_file = std::ofstream{config.output_func_path / fmt::format("funcs_{}.c", output_file_count)};
        // Write the file header
        fmt::print(current_output_file,
            "{}\n"
            "#include \"funcs.h\"\n"
            "\n",
            config.recomp_include);

        // Print the extern for the base event index and the define to rename it if exports are allowed.
        if (config.allow_exports) {
            fmt::print(current_output_file,
                "extern uint32_t builtin_base_event_index;\n"
                "#define base_event_index builtin_base_event_index\n"
                "\n"
            );
        }

        cur_file_function_count = 0;
        output_file_count++;
    };

    if (config.single_file_output) {
        current_output_file.open(config.output_func_path / config.elf_path.stem().replace_extension(".c"));
        // Write the file header
        fmt::print(current_output_file,
            "{}\n"
            "#include \"funcs.h\"\n"
            "\n",
            config.recomp_include);

        // Print the extern for the base event index and the define to rename it if exports are allowed.
        if (config.allow_exports) {
            fmt::print(current_output_file,
                "extern uint32_t builtin_base_event_index;\n"
                "#define base_event_index builtin_base_event_index\n"
                "\n"
            );
        }
    }
    else if (config.functions_per_output_file > 1) {
        open_new_output_file();
    }

    std::unordered_map<size_t, size_t> function_index_to_event_index{};

    // If exports are enabled, scan all the relocs and modify ones that point to an event function.
    if (config.allow_exports) {
        // First, find the event section by scanning for a section with the special name.
        bool event_section_found = false;
        size_t event_section_index = 0;
        uint32_t event_section_vram = 0;
        for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
            const auto& section = context.sections[section_index];
            if (section.name == N64Recomp::EventSectionName) {
                event_section_found = true;
                event_section_index = section_index;
                event_section_vram = section.ram_addr;
                break;
            }
        }

        // If an event section was found, proceed with the reloc scanning.
        if (event_section_found) {
            for (auto& section : context.sections) {
                for (auto& reloc : section.relocs) {
                    // Event symbols aren't reference symbols, since they come from the elf itself.
                    // Therefore, skip reference symbol relocs.
                    if (reloc.reference_symbol) {
                        continue;
                    }

                    // Ignore R_MIPS_NONE relocs, which get produced during symbol parsing for non-relocatable reference sections.
                    if (reloc.type == N64Recomp::RelocType::R_MIPS_NONE) {
                        continue;
                    }

                    // Check if the reloc points to the event section.
                    if (reloc.target_section == event_section_index) {
                        // It does, so find the function it's pointing at.
                        size_t func_index = context.find_function_by_vram_section(reloc.target_section_offset + event_section_vram, event_section_index);

                        if (func_index == (size_t)-1) {
                            exit_failure(fmt::format("Failed to find event function with vram {}.\n", reloc.target_section_offset + event_section_vram));
                        }

                        // Ensure the reloc is a MIPS_R_26 one before modifying it, since those are the only type allowed to reference
                        if (reloc.type != N64Recomp::RelocType::R_MIPS_26) {
                            const auto& function = context.functions[func_index];
                            exit_failure(fmt::format("Function {} is an import and cannot have its address taken.\n",
                                function.name));
                        }

                        // Check if this function has been assigned an event index already, and assign it if not.
                        size_t event_index;
                        auto find_event_it = function_index_to_event_index.find(func_index);
                        if (find_event_it != function_index_to_event_index.end()) {
                            event_index = find_event_it->second;
                        }
                        else {
                            event_index = function_index_to_event_index.size();
                            function_index_to_event_index.emplace(func_index, event_index);
                        }

                        // Modify the reloc's fields accordingly.
                        reloc.target_section_offset = 0;
                        reloc.symbol_index = event_index;
                        reloc.target_section = N64Recomp::SectionEvent;
                        reloc.reference_symbol = true;
                    }
                }
            }
        }
    }

    std::vector<size_t> export_function_indices{};

    bool failed_strict_mode = false;

    //#pragma omp parallel for
    for (size_t i = 0; i < context.functions.size(); i++) {
        const auto& func = context.functions[i];

        if (!func.ignored && func.words.size() != 0) {
            fmt::print(func_header_file,
                "void {}(uint8_t* rdram, recomp_context* ctx);\n", func.name);
            bool result;
            const auto& func_section = context.sections[func.section_index];
            // Apply strict patch mode validation if enabled.
            if (config.strict_patch_mode) {
                bool in_normal_patch_section = func_section.name == N64Recomp::PatchSectionName;
                bool in_force_patch_section = func_section.name == N64Recomp::ForcedPatchSectionName;
                bool in_patch_section = in_normal_patch_section || in_force_patch_section;
                N64Recomp::SymbolReference dummy_ref;
                bool reference_symbol_found = context.reference_symbol_exists(func.name);

                // This is a patch function, but no corresponding symbol was found in the original symbol list.
                if (in_patch_section && !reference_symbol_found) {
                    fmt::print(stderr, "Function {} is marked as a replacement, but no function with the same name was found in the reference symbols!\n", func.name);
                    failed_strict_mode = true;
                    continue;
                }
                // This is not a patch function, but it has the same name as a function in the original symbol list.
                else if (!in_patch_section && reference_symbol_found) {
                    fmt::print(stderr, "Function {} is not marked as a replacement, but a function with the same name was found in the reference symbols!\n", func.name);
                    failed_strict_mode = true;
                    continue;
                }
            }
            // Check if this is an export and add it to the list if exports are enabled.
            if (config.allow_exports && func_section.name == N64Recomp::ExportSectionName) {
                export_function_indices.push_back(i);
            }

            // Recompile the function.
            if (config.single_file_output || config.functions_per_output_file > 1) {
                result = N64Recomp::recompile_function(context, i, current_output_file, static_funcs_by_section, false);
                if (!config.single_file_output) {
                    cur_file_function_count++;
                    if (cur_file_function_count >= config.functions_per_output_file) {
                        open_new_output_file();
                    }
                }
            }
            else {
                result = recompile_single_function(context, i, config.recomp_include, config.output_func_path / (func.name + ".c"), static_funcs_by_section);
            }
            if (result == false) {
                fmt::print(stderr, "Error recompiling {}\n", func.name);
                std::exit(EXIT_FAILURE);
            }
        } else if (func.reimplemented) {
            fmt::print(func_header_file,
                       "void {}(uint8_t* rdram, recomp_context* ctx);\n", func.name);
        }
    }

    if (failed_strict_mode) {
        if (config.single_file_output || config.functions_per_output_file > 1) {
            current_output_file.close();
            std::error_code ec;
            std::filesystem::remove(config.output_func_path / config.elf_path.stem().replace_extension(".c"), ec);
        }
        exit_failure("Strict mode validation failed!\n");
    }

    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        auto& section = context.sections[section_index];
        auto& section_funcs = section.function_addrs;

        // Sort the section's functions
        std::sort(section_funcs.begin(), section_funcs.end());
        // Sort and deduplicate the static functions via a set
        std::set<uint32_t> statics_set{ static_funcs_by_section[section_index].begin(), static_funcs_by_section[section_index].end() };
        std::vector<uint32_t> section_statics{};
        section_statics.assign(statics_set.begin(), statics_set.end());

        for (size_t static_func_index = 0; static_func_index < section_statics.size(); static_func_index++) {
            uint32_t static_func_addr = section_statics[static_func_index];

            // Determine the end of this static function
            uint32_t cur_func_end = static_cast<uint32_t>(section.size + section.ram_addr);

            // Search for the closest function 
            size_t closest_func_index = 0;
            while (section_funcs[closest_func_index] < static_func_addr && closest_func_index < section_funcs.size()) {
                closest_func_index++;
            }

            // Check if there's a nonstatic function after this one
            if (closest_func_index < section_funcs.size()) {
                // If so, use that function's address as the end of this one
                cur_func_end = section_funcs[closest_func_index];
            }

            // Check for any known statics after this function and truncate this function's size to make sure it doesn't overlap.
            for (uint32_t checked_func : statics_set) {
                if (checked_func > static_func_addr && checked_func < cur_func_end) {
                    cur_func_end = checked_func;
                }
            }

            uint32_t rom_addr = static_cast<uint32_t>(static_func_addr - section.ram_addr + section.rom_addr);
            const uint32_t* func_rom_start = reinterpret_cast<const uint32_t*>(context.rom.data() + rom_addr);

            std::vector<uint32_t> insn_words((cur_func_end - static_func_addr) / sizeof(uint32_t));
            insn_words.assign(func_rom_start, func_rom_start + insn_words.size());

            // Create the new function and add it to the context.
            size_t new_func_index = context.functions.size();
            context.functions.emplace_back(
                static_func_addr,
                rom_addr,
                std::move(insn_words),
                fmt::format("static_{}_{:08X}", section_index, static_func_addr),
                static_cast<uint16_t>(section_index),
                false
            );
            const N64Recomp::Function& new_func = context.functions[new_func_index];

            fmt::print(func_header_file,
                       "void {}(uint8_t* rdram, recomp_context* ctx);\n", new_func.name);

            bool result;
            size_t prev_num_statics = static_funcs_by_section[new_func.section_index].size();
            if (config.single_file_output || config.functions_per_output_file > 1) {
                result = N64Recomp::recompile_function(context, new_func_index, current_output_file, static_funcs_by_section, false);
                if (!config.single_file_output) {
                    cur_file_function_count++;
                    if (cur_file_function_count >= config.functions_per_output_file) {
                        open_new_output_file();
                    }
                }
            }
            else {
                result = recompile_single_function(context, new_func_index, config.recomp_include, config.output_func_path / (new_func.name + ".c"), static_funcs_by_section);
            }

            // Add any new static functions that were found while recompiling this one.
            size_t cur_num_statics = static_funcs_by_section[new_func.section_index].size();
            if (cur_num_statics != prev_num_statics) {
                for (size_t new_static_index = prev_num_statics; new_static_index < cur_num_statics; new_static_index++) {
                    uint32_t new_static_vram = static_funcs_by_section[new_func.section_index][new_static_index];

                    if (!statics_set.contains(new_static_vram)) {
                        statics_set.emplace(new_static_vram);
                        section_statics.push_back(new_static_vram);
                    }
                }
            }

            if (result == false) {
                fmt::print(stderr, "Error recompiling {}\n", new_func.name);
                std::exit(EXIT_FAILURE);
            }
        }
    }

    if (config.has_entrypoint) {
        std::ofstream lookup_file{ config.output_func_path / "lookup.cpp" };
        
        fmt::print(lookup_file,
            "{}\n"
            "\n",
            config.recomp_include
        );

        fmt::print(lookup_file,
            "gpr get_entrypoint_address() {{ return (gpr)(int32_t)0x{:08X}u; }}\n"
            "\n"
            "const char* get_rom_name() {{ return \"{}\"; }}\n"
            "\n",
            static_cast<uint32_t>(config.entrypoint),
            config.elf_path.filename().replace_extension(".z64").string()
        );
    }

    {
        std::ofstream overlay_file(config.output_func_path / "recomp_overlays.inl");
        std::string section_load_table = "static SectionTableEntry section_table[] = {\n";

        fmt::print(overlay_file, 
            "{}\n"
            "#include \"funcs.h\"\n"
            "#include \"librecomp/sections.h\"\n"
            "\n",
            config.recomp_include
        );

        std::unordered_map<std::string, size_t> relocatable_section_indices{};
        size_t written_sections = 0;

        for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
            const auto& section = context.sections[section_index];
            const auto& section_funcs = context.section_functions[section_index];
            const auto& section_relocs = section.relocs;

            if (section.has_mips32_relocs || !section_funcs.empty()) {
                std::string_view section_name_trimmed{ section.name };

                if (section.relocatable) {
                    relocatable_section_indices.emplace(section.name, written_sections);
                }

                while (section_name_trimmed.size() > 0 && section_name_trimmed[0] == '.') {
                    section_name_trimmed.remove_prefix(1);
                }

                std::string section_funcs_array_name = fmt::format("section_{}_{}_funcs", section_index, section_name_trimmed);
                std::string section_relocs_array_name = section_relocs.empty() ? "nullptr" : fmt::format("section_{}_{}_relocs", section_index, section_name_trimmed);
                std::string section_relocs_array_size = section_relocs.empty() ? "0" : fmt::format("ARRLEN({})", section_relocs_array_name);

                // Write the section's table entry.
                section_load_table += fmt::format("    {{ .rom_addr = 0x{0:08X}, .ram_addr = 0x{1:08X}, .size = 0x{2:08X}, .funcs = {3}, .num_funcs = ARRLEN({3}), .relocs = {4}, .num_relocs = {5}, .index = {6} }},\n",
                                                  section.rom_addr, section.ram_addr, section.size, section_funcs_array_name,
                                                  section_relocs_array_name, section_relocs_array_size, section_index);

                // Write the section's functions.
                fmt::print(overlay_file, "static FuncEntry {}[] = {{\n", section_funcs_array_name);

                for (size_t func_index : section_funcs) {
                    const auto& func = context.functions[func_index];
                    size_t func_size = func.reimplemented ? 0 : func.words.size() * sizeof(func.words[0]);

                    if (func.reimplemented || (!func.name.empty() && !func.ignored && func.words.size() != 0)) {
                        fmt::print(overlay_file, "    {{ .func = {}, .offset = 0x{:08X}, .rom_size = 0x{:08X} }},\n",
                            func.name, func.rom - section.rom_addr, func_size);
                    }
                }

                fmt::print(overlay_file, "}};\n");

                // Write the section's relocations.
                if (!section_relocs.empty()) {
                    // Determine if reference symbols are being used.
                    bool reference_symbol_mode = !config.func_reference_syms_file_path.empty();

                    fmt::print(overlay_file, "static RelocEntry {}[] = {{\n", section_relocs_array_name);

                    for (const N64Recomp::Reloc& reloc : section_relocs) {
                        bool emit_reloc = false;
                        uint16_t target_section = reloc.target_section;
                        // In reference symbol mode, only emit relocations into the table that point to
                        // non-absolute reference symbols, events, or manual patch symbols.
                        if (reference_symbol_mode) {
                            bool manual_patch_symbol = N64Recomp::is_manual_patch_symbol(reloc.target_section_offset);
                            bool is_absolute = reloc.target_section == N64Recomp::SectionAbsolute;
                            emit_reloc = (reloc.reference_symbol && !is_absolute) || target_section == N64Recomp::SectionEvent || manual_patch_symbol;
                        }
                        // Otherwise, emit all relocs.
                        else {
                            emit_reloc = true;
                        }
                        if (emit_reloc) {
                            uint32_t target_section_offset;
                            if (reloc.target_section == N64Recomp::SectionEvent) {
                                target_section_offset = reloc.symbol_index;
                            }
                            else {
                                target_section_offset = reloc.target_section_offset;
                            }
                            fmt::print(overlay_file, "    {{ .offset = 0x{:08X}, .target_section_offset = 0x{:08X}, .target_section = {}, .type = {} }}, \n",
                                reloc.address - section.ram_addr, target_section_offset, reloc.target_section, reloc_names[static_cast<size_t>(reloc.type)] );
                        }
                    }

                    fmt::print(overlay_file, "}};\n");
                }

                written_sections++;
            }
        }
        section_load_table += "};\n";

        fmt::print(overlay_file, "{}", section_load_table);

        fmt::print(overlay_file, "const size_t num_sections = {};\n", context.sections.size());


        fmt::print(overlay_file, "static int overlay_sections_by_index[] = {{\n");
        if (relocatable_sections_ordered.empty()) {
            fmt::print(overlay_file, "    -1,\n");
        } else {
            for (const std::string& section : relocatable_sections_ordered) {
                // Check if this is an empty overlay
                if (section == "*") {
                    fmt::print(overlay_file, "    -1,\n");
                }
                else {
                    auto find_it = relocatable_section_indices.find(section);
                    if (find_it == relocatable_section_indices.end()) {
                        fmt::print(stderr, "Failed to find written section index of relocatable section: {}\n", section);
                        std::exit(EXIT_FAILURE);
                    }
                    fmt::print(overlay_file, "    {},\n", relocatable_section_indices[section]);
                }
            }
        }
        fmt::print(overlay_file, "}};\n");

        if (config.allow_exports) {
            // Emit the exported function table.
            fmt::print(overlay_file, 
                "\n"
                "static FunctionExport export_table[] = {{\n"
            );
            for (size_t func_index : export_function_indices) {
                const auto& func = context.functions[func_index];
                fmt::print(overlay_file, "    {{ \"{}\", 0x{:08X} }},\n", func.name, func.vram);
            }
            // Add a dummy element at the end to ensure the array has a valid length because C doesn't allow zero-size arrays.
            fmt::print(overlay_file, "    {{ NULL, 0 }}\n");
            fmt::print(overlay_file, "}};\n");

            // Emit the event table.
            std::vector<size_t> functions_by_event{};
            functions_by_event.resize(function_index_to_event_index.size());
            for (auto [func_index, event_index] : function_index_to_event_index) {
                functions_by_event[event_index] = func_index;
            }

            fmt::print(overlay_file,
                "\n"
                "static const char* event_names[] = {{\n"
            );
            for (size_t func_index : functions_by_event) {
                const auto& func = context.functions[func_index];
                fmt::print(overlay_file, "    \"{}\",\n", func.name);
            }
            // Add a dummy element at the end to ensure the array has a valid length because C doesn't allow zero-size arrays.
            fmt::print(overlay_file, "    NULL\n");
            fmt::print(overlay_file, "}};\n");

            // Collect manual patch symbols.
            std::vector<std::pair<uint32_t, std::string>> manual_patch_syms{};

            for (const auto& func : context.functions) {
                if (func.words.empty() && N64Recomp::is_manual_patch_symbol(func.vram)) {
                    manual_patch_syms.emplace_back(func.vram, func.name);
                }
            }            

            // Sort the manual patch symbols by vram.
            std::sort(manual_patch_syms.begin(), manual_patch_syms.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });

            // Emit the manual patch symbols.
            fmt::print(overlay_file,
                "\n"
                "static const ManualPatchSymbol manual_patch_symbols[] = {{\n"
            );
            for (const auto& manual_patch_sym_entry : manual_patch_syms) {
                fmt::print(overlay_file, "    {{ 0x{:08X}, {} }},\n", manual_patch_sym_entry.first, manual_patch_sym_entry.second);

                fmt::print(func_header_file,
                    "void {}(uint8_t* rdram, recomp_context* ctx);\n", manual_patch_sym_entry.second);
            }
            // Add a dummy element at the end to ensure the array has a valid length because C doesn't allow zero-size arrays.
            fmt::print(overlay_file, "    {{ 0, NULL }}\n");
            fmt::print(overlay_file, "}};\n");
        }
    }

    fmt::print(func_header_file,
        "\n"
        "#ifdef __cplusplus\n"
        "}}\n"
        "#endif\n"
    );

    if (!config.output_binary_path.empty()) {
        std::ofstream output_binary{config.output_binary_path, std::ios::binary};
        output_binary.write(reinterpret_cast<const char*>(context.rom.data()), context.rom.size());
    }

    return 0;
}
