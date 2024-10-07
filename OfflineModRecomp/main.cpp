#include <filesystem>
#include <fstream>
#include <vector>
#include <span>

#include "recompiler/context.h"
#include "rabbitizer.hpp"

static std::vector<uint8_t> read_file(const std::filesystem::path& path, bool& found) {
    std::vector<uint8_t> ret;
    found = false;

    std::ifstream file{ path, std::ios::binary};

    if (file.good()) {
        file.seekg(0, std::ios::end);
        ret.resize(file.tellg());
        file.seekg(0, std::ios::beg);

        file.read(reinterpret_cast<char*>(ret.data()), ret.size());
        found = true;
    }

    return ret;
}

int main(int argc, const char** argv) {
    if (argc != 5) {
        printf("Usage: %s [mod symbol file] [mod binary file] [recomp symbols file] [output C file]\n", argv[0]);
        return EXIT_SUCCESS;
    }
    bool found;
    std::vector<uint8_t> symbol_data = read_file(argv[1], found);
    if (!found) {
        fprintf(stderr, "Failed to open symbol file\n");
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> rom_data = read_file(argv[2], found);
    if (!found) {
        fprintf(stderr, "Failed to open ROM\n");
        return EXIT_FAILURE;
    }

    std::span<const char> symbol_data_span { reinterpret_cast<const char*>(symbol_data.data()), symbol_data.size() };

    std::vector<uint8_t> dummy_rom{};
    N64Recomp::Context reference_context{};
    if (!N64Recomp::Context::from_symbol_file(argv[3], std::move(dummy_rom), reference_context, false)) {
        printf("Failed to load provided function reference symbol file\n");
        return EXIT_FAILURE;
    }

    //for (const std::filesystem::path& cur_data_sym_path : data_reference_syms_file_paths) {
    //    if (!reference_context.read_data_reference_syms(cur_data_sym_path)) {
    //        printf("Failed to load provided data reference symbol file\n");
    //        return EXIT_FAILURE;
    //    }
    //}

    std::unordered_map<uint32_t, uint16_t> sections_by_vrom{};
    for (uint16_t section_index = 0; section_index < reference_context.sections.size(); section_index++) {
        sections_by_vrom[reference_context.sections[section_index].rom_addr] = section_index;
    }

    N64Recomp::Context mod_context;

	N64Recomp::ModSymbolsError error = N64Recomp::parse_mod_symbols(symbol_data_span, rom_data, sections_by_vrom, mod_context);
    if (error != N64Recomp::ModSymbolsError::Good) {
        fprintf(stderr, "Error parsing mod symbols: %d\n", (int)error);
        return EXIT_FAILURE;
    }

    mod_context.import_reference_context(reference_context);

    // Populate R_MIPS_26 reloc symbol indices. Start by building a map of vram address to matching reference symbols.
    std::unordered_map<uint32_t, std::vector<size_t>> reference_symbols_by_vram{};
    for (size_t reference_symbol_index = 0; reference_symbol_index < mod_context.num_regular_reference_symbols(); reference_symbol_index++) {
        const auto& sym = mod_context.get_regular_reference_symbol(reference_symbol_index);
        uint16_t section_index = sym.section_index;
        if (section_index != N64Recomp::SectionAbsolute) {
            uint32_t section_vram = mod_context.get_reference_section_vram(section_index);
            reference_symbols_by_vram[section_vram + sym.section_offset].push_back(reference_symbol_index);
        }
    }
    
    // Use the mapping to populate the symbol index for every R_MIPS_26 reference symbol reloc. 
    for (auto& section : mod_context.sections) {
        for (auto& reloc : section.relocs) {
            if (reloc.type == N64Recomp::RelocType::R_MIPS_26 && reloc.reference_symbol) {
                if (mod_context.is_regular_reference_section(reloc.target_section)) {
                    uint32_t section_vram = mod_context.get_reference_section_vram(reloc.target_section);
                    uint32_t target_vram = section_vram + reloc.target_section_offset;

                    auto find_funcs_it = reference_symbols_by_vram.find(target_vram);
                    bool found = false;
                    if (find_funcs_it != reference_symbols_by_vram.end()) {
                        for (size_t symbol_index : find_funcs_it->second) {
                            const auto& cur_symbol = mod_context.get_reference_symbol(reloc.target_section, symbol_index);
                            if (cur_symbol.section_index == reloc.target_section) {
                                reloc.symbol_index = symbol_index;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Failed to find R_MIPS_26 relocation target in section %d with vram 0x%08X\n", reloc.target_section, target_vram);
                        return EXIT_FAILURE;
                    }
                }
            }
        }
    }

    mod_context.rom = std::move(rom_data);

    std::vector<std::vector<uint32_t>> static_funcs_by_section{};
    static_funcs_by_section.resize(mod_context.sections.size());

    const char* output_file_path = argv[4];
    std::ofstream output_file { output_file_path };

    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;

    output_file << "#include \"mod_recomp.h\"\n\n";

    // Write the API version.
    output_file << "RECOMP_EXPORT uint32_t recomp_api_version = 1;\n\n";

    output_file << "// Values populated by the runtime:\n\n";

    // Write import function pointer array and defines (i.e. `#define testmod_inner_import imported_funcs[0]`)
    output_file << "// Array of pointers to imported functions with defines to alias their names.\n";
    size_t num_imports = mod_context.import_symbols.size();
    for (size_t import_index = 0; import_index < num_imports; import_index++) {
        const auto& import = mod_context.import_symbols[import_index];
        output_file << "#define " << import.base.name << " imported_funcs[" << import_index << "]\n";
    }

    output_file << "RECOMP_EXPORT recomp_func_t* imported_funcs[" << std::max(size_t{1}, num_imports) << "] = {0};\n";
    output_file << "\n";

    // Use reloc list to write reference symbol function pointer array and defines (i.e. `#define func_80102468 reference_symbol_funcs[0]`)
    output_file << "// Array of pointers to functions from the original ROM with defines to alias their names.\n";
    std::unordered_set<std::string> written_reference_symbols{};
    size_t num_reference_symbols = 0;
    for (const auto& section : mod_context.sections) {
        for (const auto& reloc : section.relocs) {
            if (reloc.type == N64Recomp::RelocType::R_MIPS_26 && reloc.reference_symbol && mod_context.is_regular_reference_section(reloc.target_section)) {
                const auto& sym = mod_context.get_reference_symbol(reloc.target_section, reloc.symbol_index);

                // Prevent writing multiple of the same define. This means there are duplicate symbols in the array if a function is called more than once,
                // but only the first of each set of duplicates is referenced. This is acceptable, since offline mod recompilation is mainly meant for debug purposes.
                if (!written_reference_symbols.contains(sym.name)) {
                    output_file << "#define " << sym.name << " reference_symbol_funcs[" << num_reference_symbols << "]\n";
                    written_reference_symbols.emplace(sym.name);
                }
                num_reference_symbols++;
            }
        }
    }
    // C doesn't allow 0-sized arrays, so always add at least one member to all arrays. The actual size will be pulled from the mod symbols.
    output_file << "RECOMP_EXPORT recomp_func_t* reference_symbol_funcs[" << std::max(size_t{1},num_reference_symbols) << "] = {0};\n\n";

    // Write provided event array (maps internal event indices to global ones).
    output_file << "// Base global event index for this mod's events.\n";
    output_file << "RECOMP_EXPORT uint32_t base_event_index;\n\n";

    // Write the event trigger function pointer.
    output_file << "// Pointer to the runtime function for triggering events.\n";
    output_file << "RECOMP_EXPORT void (*recomp_trigger_event)(uint8_t* rdram, recomp_context* ctx, uint32_t) = NULL;\n\n";

    // Write the get_function pointer.
    output_file << "// Pointer to the runtime function for looking up functions from vram address.\n";
    output_file << "RECOMP_EXPORT recomp_func_t* (*get_function)(int32_t vram) = NULL;\n\n";

    // Write the cop0_status_write pointer.
    output_file << "// Pointer to the runtime function for performing a cop0 status register write.\n";
    output_file << "RECOMP_EXPORT void (*cop0_status_write)(recomp_context* ctx, gpr value) = NULL;\n\n";

    // Write the cop0_status_read pointer.
    output_file << "// Pointer to the runtime function for performing a cop0 status register read.\n";
    output_file << "RECOMP_EXPORT gpr (*cop0_status_read)(recomp_context* ctx) = NULL;\n\n";

    // Write the switch_error pointer.
    output_file << "// Pointer to the runtime function for reporting switch case errors.\n";
    output_file << "RECOMP_EXPORT void (*switch_error)(const char* func, uint32_t vram, uint32_t jtbl) = NULL;\n\n";

    // Write the do_break pointer.
    output_file << "// Pointer to the runtime function for handling the break instruction.\n";
    output_file << "RECOMP_EXPORT void (*do_break)(uint32_t vram) = NULL;\n\n";

    // Write the section_addresses pointer.
    output_file << "// Pointer to the runtime's array of loaded section addresses for the base ROM.\n";
    output_file << "RECOMP_EXPORT int32_t* reference_section_addresses = NULL;\n\n";

    // Write the local section addresses pointer array.
    size_t num_sections = mod_context.sections.size();
    output_file << "// Array of this mod's loaded section addresses.\n";
    output_file << "RECOMP_EXPORT int32_t section_addresses[" << std::max(size_t{1}, num_sections) << "] = {0};\n\n";

    // Create a set of the export indices to avoid renaming them.
    std::unordered_set<size_t> export_indices{mod_context.exported_funcs.begin(), mod_context.exported_funcs.end()};

    // Name all the functions in a first pass so function calls emitted in the second are correct. Also emit function prototypes.
    output_file << "// Function prototypes.\n";
    for (size_t func_index = 0; func_index < mod_context.functions.size(); func_index++) {
        auto& func = mod_context.functions[func_index];
        // Don't rename exports since they already have a name from the mod symbol file.
        if (!export_indices.contains(func_index)) {
            func.name = "mod_func_" + std::to_string(func_index);
        }
        output_file << "RECOMP_FUNC void " << func.name << "(uint8_t* rdram, recomp_context* ctx);\n";
    }
    output_file << "\n";

    // Perform a second pass for recompiling all the functions.
    for (size_t func_index = 0; func_index < mod_context.functions.size(); func_index++) {
        if (!N64Recomp::recompile_function(mod_context, func_index, output_file, static_funcs_by_section, true)) {
            output_file.close();
            std::error_code ec;
            std::filesystem::remove(output_file_path, ec);
            return EXIT_FAILURE;
        }
    }

	return EXIT_SUCCESS;
}
