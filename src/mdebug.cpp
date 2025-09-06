#include <unordered_map>

#include "fmt/format.h"

#include "mdebug.h"

struct MDebugSymbol {
    std::string name;
    uint32_t address;
    uint32_t size;
    bool is_func;
    bool is_static;
    bool is_bss;
    bool is_rodata;
    bool ignored;
};

struct MDebugFile {
    std::string filename;
    std::vector<MDebugSymbol> symbols;
};

class MDebugInfo {
public:
    MDebugInfo(const N64Recomp::ElfParsingConfig& config, const char* mdebug_section, uint32_t mdebug_offset) {
        using namespace N64Recomp;
        good_ = false;
        // Read, byteswap and relocate the symbolic header. Relocation here means convert file-relative offsets to section-relative offsets.

        MDebug::HDRR hdrr;
        std::memcpy(&hdrr, mdebug_section, sizeof(MDebug::HDRR));
        hdrr.swap();
        hdrr.relocate(mdebug_offset);

        // Check the magic value and version number are what we expect.

        if (hdrr.magic != MDebug::MAGIC || hdrr.vstamp != 0) {
            fmt::print(stderr, "Warning: Found an mdebug section with bad magic value or version (magic={} version={}). Skipping.\n", hdrr.magic, hdrr.vstamp);
            return;
        }

        // Read the various records that are relevant for collecting static symbols and where they are declared.

        std::vector<MDebug::FDR> fdrs = hdrr.read_fdrs(mdebug_section);
        std::vector<MDebug::AUX> all_auxs = hdrr.read_auxs(mdebug_section);
        std::vector<MDebug::PDR> all_pdrs = hdrr.read_pdrs(mdebug_section);
        std::vector<MDebug::SYMR> all_symrs = hdrr.read_symrs(mdebug_section);
        
        // For each file descriptor
        for (size_t fdr_index = 0; fdr_index < fdrs.size(); fdr_index++) {
            MDebug::FDR& fdr = fdrs[fdr_index];
            MDebugSymbol pending_sym{};
            bool pending_sym_ready = false;

            auto flush_pending_sym = [&]() {
                if (pending_sym_ready) {
                    // Handle ignored symbols.
                    pending_sym.ignored = config.ignored_syms.contains(pending_sym.name);
                    // Add the symbol.
                    add_symbol(fdr_index, std::move(pending_sym));
                }
                pending_sym_ready = false;
            };

            const char* fdr_name = fdr.get_name(mdebug_section + hdrr.cbSsOffset);
            add_file(fdr_name);

            // For every symbol record in the file descriptor record.
            for (auto symr : fdr.get_symrs(all_symrs)) {
                MDebug::ST type = symr.get_st();
                MDebug::SC storage_class = symr.get_sc();

                switch (type) {
                    case MDebug::ST_PROC:
                    case MDebug::ST_STATICPROC:
                        flush_pending_sym();
                        if (symr.value != 0) {
                            pending_sym.name = fdr.get_string(mdebug_section + hdrr.cbSsOffset, symr.iss);
                            pending_sym.address = symr.value;
                            pending_sym.size = 0;
                            pending_sym.is_func = true;
                            pending_sym.is_static = (type == MDebug::ST_STATICPROC);
                            pending_sym.is_bss = false;
                            pending_sym.is_rodata = false;
                            pending_sym_ready = true;
                        }
                        break;
                    case MDebug::ST_END:
                        if (pending_sym.is_func) {
                            pending_sym.size = symr.value;
                        }
                        flush_pending_sym();
                        break;
                    case MDebug::ST_GLOBAL:
                    case MDebug::ST_STATIC:
                        flush_pending_sym();
                        if (symr.value != 0) {
                            pending_sym.name = fdr.get_string(mdebug_section + hdrr.cbSsOffset, symr.iss);
                            pending_sym.address = symr.value;
                            pending_sym.size = 0;
                            pending_sym.is_func = false;
                            pending_sym.is_static = (type == MDebug::ST_STATIC);
                            pending_sym.is_bss = (storage_class == MDebug::SC_BSS);
                            pending_sym.is_rodata = (storage_class == MDebug::SC_RDATA);
                            pending_sym_ready = true;
                        }
                        break;
                    default:
                        flush_pending_sym();
                        break;
                }
            }
            flush_pending_sym();
        }
        good_ = true;
    }

    bool is_identifier_char(char c) {
        if (c >= 'a' && c <= 'z') {
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            return true;
        }
        if (c == '_') {
            return true;
        }
        if (c >= '0' && c <= '9') {
            return true;
        }
        return false;
    }

    std::string sanitize_section_name(std::string section_name) {
        // Skip periods at the start of the section name.
        size_t start_pos = 0;
        while (section_name[start_pos] == '.' && start_pos < section_name.size()) {
            start_pos++;
        }

        std::string ret = section_name.substr(start_pos);
        for (size_t char_index = 0; char_index < ret.size(); char_index++) {
            if (!is_identifier_char(ret[char_index])) {
                ret[char_index] = '_';
            }
        }
        return ret;
    }

    bool populate_context(const N64Recomp::ElfParsingConfig& elf_config, N64Recomp::Context& context, N64Recomp::DataSymbolMap& data_syms) {
        size_t num_files = files_.size();
        std::vector<uint16_t> file_text_sections{};
        std::vector<uint16_t> file_data_sections{};
        std::vector<uint16_t> file_rodata_sections{};
        std::vector<uint16_t> file_bss_sections{};
        file_text_sections.resize(num_files, (uint16_t)-1);
        file_data_sections.resize(num_files, (uint16_t)-1);
        file_rodata_sections.resize(num_files, (uint16_t)-1);
        file_bss_sections.resize(num_files, (uint16_t)-1);
        std::unordered_map<std::string, std::vector<size_t>> mdebug_symbol_names{}; // Maps symbol name to list of files that have a symbol of that name.

        // Build a lookup of section name to elf section index.
        std::unordered_map<std::string, uint16_t> elf_sections_by_name{};
        for (uint16_t section_index = 0; section_index < context.sections.size(); section_index++) {
            const N64Recomp::Section& section = context.sections[section_index];
            elf_sections_by_name.emplace(section.name, section_index);
        }

        // First pass to collect symbol names and map mdebug files to elf sections.
        for (size_t file_index = 0; file_index < num_files; file_index++) {
            const MDebugFile& file = files_[file_index];

            if (file.symbols.empty()) {
                continue;
            }

            bool has_funcs = false;
            bool has_data = false;
            bool has_rodata = false;
            bool has_bss = false;
            // Find the section that this file's .text was placed into by looking up global functions.
            int file_text_section = -1;
            uint32_t min_data_address = 0x0;
            uint32_t max_data_address = 0x0;
            uint32_t min_rodata_address = 0x0;
            uint32_t max_rodata_address = 0x0;
            uint32_t min_bss_address = 0x0;
            uint32_t max_bss_address = 0x0;
            for (const auto& sym : file.symbols) {
                if (sym.ignored) {
                    continue;
                }
                mdebug_symbol_names[sym.name].emplace_back(file_index);
                if (sym.is_func) {
                    has_funcs = true;
                    if (!sym.is_static) {
                        auto find_it = context.functions_by_name.find(sym.name);
                        if (find_it != context.functions_by_name.end()) {
                            const auto& found_sym = context.functions[find_it->second];
                            file_text_section = found_sym.section_index;
                        }
                    }
                }
                else {
                    if (sym.address != 0) {
                        // .bss
                        if (sym.is_bss) {
                            has_bss = true;
                            if (min_bss_address == 0) {
                                min_bss_address = sym.address;
                            }
                            else {
                                min_bss_address = std::min(min_bss_address, sym.address);
                            }
                            max_bss_address = std::max(max_bss_address, sym.address + sym.size);
                        }
                        // .rodata
                        else if (sym.is_rodata) {
                            has_rodata = true;
                            if (min_rodata_address == 0) {
                                min_rodata_address = sym.address;
                            }
                            else {
                                min_rodata_address = std::min(min_rodata_address, sym.address);
                            }
                            max_rodata_address = std::max(max_rodata_address, sym.address + sym.size);
                        }
                        // .data
                        else {
                            has_data = true;
                            if (min_data_address == 0) {
                                min_data_address = sym.address;
                            }
                            else {
                                min_data_address = std::min(min_data_address, sym.address);
                            }
                            max_data_address = std::max(max_data_address, sym.address + sym.size);
                        }
                    }
                }
            }

            if (!has_funcs) {
                continue;
            }

            // Manual file text section mapping.
            {
                auto find_text_mapping_it = elf_config.mdebug_text_map.find(file.filename);
                if (find_text_mapping_it != elf_config.mdebug_text_map.end()) {
                    auto find_text_section_it = elf_sections_by_name.find(find_text_mapping_it->second);
                    if (find_text_section_it == elf_sections_by_name.end()) {
                        printf(".text section for mdebug source file \"%s\" is mapped to section \"%s\", which doesn't exist in the elf\n", file.filename.c_str(), find_text_mapping_it->second.c_str());
                        return false;
                    }
                    file_text_section = find_text_section_it->second;
                }
            }

            if (file_text_section == -1) {
                printf("Couldn't determine elf section of mdebug info for file %s\n", file.filename.c_str());
                return false;
            }

            file_text_sections[file_index] = file_text_section;
            const N64Recomp::Section& text_section = context.sections[file_text_section];

            if (has_data) {
                uint16_t file_data_section;
                
                // Manual file data section mapping.
                auto find_data_mapping_it = elf_config.mdebug_data_map.find(file.filename);
                if (find_data_mapping_it != elf_config.mdebug_data_map.end()) {
                    auto find_data_section_it = elf_sections_by_name.find(find_data_mapping_it->second);
                    if (find_data_section_it == elf_sections_by_name.end()) {
                        printf(".data section for mdebug source file \"%s\" is mapped to section \"%s\", which doesn't exist in the elf\n", file.filename.c_str(), find_data_mapping_it->second.c_str());
                        return false;
                    }
                    file_data_section = find_data_section_it->second;
                }
                // Automatic mapping, attempt to use the same section that .text was placed in.
                else {
                    if (min_data_address < text_section.ram_addr || max_data_address > text_section.ram_addr + text_section.size) {
                        printf("File %s has static data in mdebug which did not overlap with section %s\n", file.filename.c_str(), text_section.name.c_str());
                        return false;
                    }
                    file_data_section = file_text_section;
                }

                file_data_sections[file_index] = file_data_section;
            }

            if (has_rodata) {
                uint16_t file_rodata_section;;
                
                // Manual file rodata section mapping.
                auto find_rodata_mapping_it = elf_config.mdebug_data_map.find(file.filename);
                if (find_rodata_mapping_it != elf_config.mdebug_data_map.end()) {
                    auto find_rodata_section_it = elf_sections_by_name.find(find_rodata_mapping_it->second);
                    if (find_rodata_section_it == elf_sections_by_name.end()) {
                        printf(".rodata section for mdebug source file \"%s\" is mapped to section \"%s\", which doesn't exist in the elf\n", file.filename.c_str(), find_rodata_mapping_it->second.c_str());
                        return false;
                    }
                    file_rodata_section = find_rodata_section_it->second;
                }
                // Automatic mapping, attempt to use the same section that .text was placed in.
                else {
                    if (min_rodata_address < text_section.ram_addr || max_rodata_address > text_section.ram_addr + text_section.size) {
                        printf("File %s has static rodata in mdebug which did not overlap with section %s\n", file.filename.c_str(), text_section.name.c_str());
                        return false;
                    }
                    file_rodata_section = file_text_section;
                }

                file_rodata_sections[file_index] = file_rodata_section;
            }
            
            if (has_bss) {
                uint16_t file_bss_section;

                // Manual file bss section mapping.
                auto find_bss_mapping_it = elf_config.mdebug_data_map.find(file.filename);
                if (find_bss_mapping_it != elf_config.mdebug_data_map.end()) {
                    auto find_bss_section_it = elf_sections_by_name.find(find_bss_mapping_it->second);
                    if (find_bss_section_it == elf_sections_by_name.end()) {
                        printf(".bss section for mdebug source file \"%s\" is mapped to section \"%s\", which doesn't exist in the elf\n", file.filename.c_str(), find_bss_mapping_it->second.c_str());
                        return false;
                    }
                    file_bss_section = find_bss_section_it->second;
                }
                // Automatic mapping, attempt to use the corresponding bss section for the section .text was placed in.
                else {
                    if (text_section.bss_section_index == (uint16_t)-1) {
                        printf("File %s has static bss in mdebug but no paired bss section. Use the \"bss_section_suffix\" option to pair bss sections.\n", file.filename.c_str());
                        return false;
                    }
                    const N64Recomp::Section& bss_section = context.sections[text_section.bss_section_index];
                    if (min_bss_address < bss_section.ram_addr || max_bss_address > bss_section.ram_addr + bss_section.size) {
                        printf("File %s has static bss in mdebug which did not overlap with bss section %s\n", file.filename.c_str(), bss_section.name.c_str());
                        return false;
                    }
                    file_bss_section = text_section.bss_section_index;
                }

                file_bss_sections[file_index] = file_bss_section;
            }
        }

        // Maps symbol name to list of sections that will receive a symbol with that name. 
        std::unordered_map<std::string, std::vector<uint16_t>> symbol_name_to_sections; 
        // Elf section of each mdebug symbol, indexed by file index and then symbol index.
        std::vector<std::vector<uint16_t>> file_symbol_sections;
        file_symbol_sections.resize(num_files);

        // Second pass to assign symbols to elf sections. This allows the third pass to rename symbols if necessary.
        // This has to be done after the first pass as we don't know section indices while processing symbols at that point.
        for (size_t file_index = 0; file_index < num_files; file_index++) {
            const MDebugFile& file = files_[file_index];
            file_symbol_sections[file_index].resize(file.symbols.size());
            for (size_t sym_index = 0; sym_index < file.symbols.size(); sym_index++) {
                const MDebugSymbol& sym = file.symbols[sym_index];
                if (sym.ignored) {
                    continue;
                }
                if (sym.is_static) {
                    uint16_t sym_section;
                    // Static .text
                    if (sym.is_func) {
                        sym_section = file_text_sections[file_index];
                    }
                    // Static .bss
                    else if (sym.is_bss) {
                        sym_section = file_text_sections[file_index];
                    }
                    // Static .data/.rodata
                    else {
                        sym_section = file_text_sections[file_index];
                    }
                    symbol_name_to_sections[sym.name].emplace_back(sym_section);
                    file_symbol_sections[file_index][sym_index] = sym_section;
                }
            }
        }

        // Mapping of datasym name to section.
        std::unordered_map<std::string, uint16_t> datasyms_by_name{};
        for (const auto &[section_index, section_datasyms] : data_syms) {
            for (const auto& datasym : section_datasyms) {
                datasyms_by_name.emplace(datasym.name, section_index);
            }
        }

        // Third pass to populate the context and data symbol map, renaming symbols as needed to avoid conflicts.
        for (size_t file_index = 0; file_index < num_files; file_index++) {
            const MDebugFile& file = files_[file_index];
            for (size_t sym_index = 0; sym_index < file.symbols.size(); sym_index++) {
                const MDebugSymbol& sym = file.symbols[sym_index];
                if (sym.ignored) {
                    continue;
                }

                uint16_t sym_section = file_symbol_sections[file_index][sym_index];
                
                // Skip symbols with no section. This should only apply to static data/bss symbols in a file that had no functions.
                if (sym_section == (uint16_t)-1) {
                    continue;
                }

                if (sym.is_static) {
                    bool already_exists = false;
                    bool already_exists_in_section = false;

                    // Check if the symbol name exists in the base symbol list already.
                    auto find_in_context_it = context.functions_by_name.find(sym.name);
                    if (find_in_context_it != context.functions_by_name.end()) {
                        already_exists = true;
                        uint16_t found_section_index = context.functions[find_in_context_it->second].section_index;
                        if (sym_section == found_section_index) {
                            already_exists_in_section = true;
                        }
                    }

                    // Check if the symbol name exists in the data syms already.
                    auto find_in_datasyms_it = datasyms_by_name.find(sym.name);
                    if (find_in_datasyms_it != datasyms_by_name.end()) {
                        already_exists = true;
                        uint16_t found_section_index = find_in_datasyms_it->second;
                        if (sym_section == found_section_index) {
                            already_exists_in_section = true;
                        }
                    }

                    // Check if the symbol name exists in the mdebug symbols.
                    auto find_in_mdebug_it = symbol_name_to_sections.find(sym.name);
                    if (find_in_mdebug_it != symbol_name_to_sections.end()) {
                        const std::vector<uint16_t>& section_list = find_in_mdebug_it->second;
                        size_t count_in_section = std::count(section_list.begin(), section_list.end(), sym_section);
                        // The count will always be at least one because of this symbol itself, so check that it's greater than one for duplicates.
                        if (count_in_section > 1) {
                            already_exists_in_section = true;
                        }
                        // If the symbol name shows up in multiple sections, rename it.
                        else if (section_list.size() > 1) {
                            already_exists = true;
                        }
                    }

                    std::string sym_output_name = sym.name;
                    if (already_exists_in_section) {
                        sym_output_name += fmt::format("_{}_{:08X}", sanitize_section_name(context.sections[sym_section].name), sym.address);
                        printf("Renamed static symbol \"%s\" to \"%s\"\n", sym.name.c_str(), sym_output_name.c_str());
                    }
                    else if (already_exists) {
                        sym_output_name += "_" + sanitize_section_name(context.sections[sym_section].name);
                        printf("Renamed static symbol \"%s\" to \"%s\"\n", sym.name.c_str(), sym_output_name.c_str());
                    }

                    // Emit the symbol.
                    if (sym.is_func) {
                        uint32_t section_vram = context.sections[sym_section].ram_addr;
                        uint32_t section_offset = sym.address - section_vram;
                        uint32_t rom_address = static_cast<uint32_t>(section_offset + context.sections[sym_section].rom_addr);
                        const uint32_t* words = reinterpret_cast<const uint32_t*>(context.rom.data() + rom_address);
                        uint32_t num_instructions = sym.size / sizeof(uint32_t);

                        std::vector<uint32_t> insn_words(num_instructions);
                        insn_words.assign(words, words + num_instructions);

                        context.functions_by_vram[sym.address].push_back(context.functions.size());
                        context.section_functions[sym_section].push_back(context.functions.size());
                        context.functions.emplace_back(N64Recomp::Function{
                            sym.address,
                            rom_address,
                            std::move(insn_words),
                            std::move(sym_output_name),
                            sym_section,
                            // TODO read these from elf config.
                            false, // ignored
                            false, // reimplemented
                            false, // stubbed
                        });
                    }
                    else {
                        data_syms[sym_section].emplace_back(N64Recomp::DataSymbol {
                            sym.address,
                            std::move(sym_output_name) 
                        });
                    }
                }
            }
        }

        return true;
    }
    void print() {
        printf("Mdebug Info\n");
        for (const auto& file : files_) {
            printf("  File %s\n", file.filename.c_str());
            for (const auto& symbol : file.symbols) {
                printf("    %s @ 0x%08X (size 0x%08X)\n", symbol.name.c_str(), symbol.address, symbol.size);
            }
        }
    }
    bool good() {
        return good_;
    }
private:
    void add_file(std::string&& filename) { 
        files_.emplace_back(MDebugFile{
            .filename = std::move(filename),
            .symbols = {}
        });
    }
    void add_symbol(size_t file_index, MDebugSymbol&& sym) {
        symbols_by_name_.emplace(sym.name, std::make_pair(file_index, files_[file_index].symbols.size()));
        files_[file_index].symbols.emplace_back(std::move(sym));
    }
    std::vector<MDebugFile> files_;
    // Map of symbol name to file index, symbol index. Multimap because multiple symbols may have the same name due to statics.
    std::unordered_multimap<std::string, std::pair<size_t, size_t>> symbols_by_name_;
    bool good_ = false;
};

#if 0
bool get_func(const char *mdata, const MDebug::HDRR& hdrr, const MDebug::FDR& fdr, const MDebug::PDR& pdr, const std::vector<MDebug::AUX>& all_auxs, std::span<const MDebug::SYMR> all_symrs, N64Recomp::Function& func_out) {
    func_out = {};

    std::pair<uint32_t, uint32_t> sym_bounds = pdr.sym_bounds(all_symrs, all_auxs);
    std::span<const MDebug::SYMR> fdr_symrs = fdr.get_symrs(all_symrs);

    for (uint32_t i = sym_bounds.first; i < sym_bounds.second; i++) {
        const MDebug::SYMR& symr = fdr_symrs[i];
        MDebug::ST type = symr.get_st();

        if (type == MDebug::ST_PROC || type == MDebug::ST_STATICPROC) {
            const char* name = fdr.get_string(mdata + hdrr.cbSsOffset, symr.iss);
            printf("    %s\n", name);
        }
        else if (type == MDebug::ST_END) {
            printf("      %08lX\n", symr.value);
        }
    }

    fflush(stdout);
    return true;
}
void read_mdebug(N64Recomp::Context& context, ELFIO::section* mdebug_section, std::unordered_map<uint16_t, std::vector<N64Recomp::DataSymbol>>& data_syms) {
    if (mdebug_section == nullptr) {
        return;
    }

    ELFIO::Elf64_Off base_offset = mdebug_section->get_offset();
    const char *mdata = mdebug_section->get_data();

    // Read, byteswap and relocate the symbolic header. Relocation here means convert file-relative offsets to section-relative offsets.

    MDebug::HDRR hdrr;
    std::memcpy(&hdrr, mdata, sizeof(MDebug::HDRR));
    hdrr.swap();
    hdrr.relocate(base_offset);

    // Check the magic value and version number are what we expect.

    if (hdrr.magic != MDebug::MAGIC || hdrr.vstamp != 0) {
        fmt::print(stderr, "Warning: Found an mdebug section with bad magic value or version (magic={} version={}). Skipping.\n", hdrr.magic, hdrr.vstamp);
        return;
    }

    // Read the various records that are relevant for collecting static symbols and where they are declared.

    std::vector<MDebug::FDR> fdrs = hdrr.read_fdrs(mdata);
    std::vector<MDebug::AUX> all_auxs = hdrr.read_auxs(mdata);
    std::vector<MDebug::PDR> all_pdrs = hdrr.read_pdrs(mdata);
    std::vector<MDebug::SYMR> all_symrs = hdrr.read_symrs(mdata);

    // For each file descriptor
    for (MDebug::FDR& fdr : fdrs) {
        const char* fdr_name = fdr.get_name(mdata + hdrr.cbSsOffset);

        printf("%s @ 0x%08X:\n", fdr_name, fdr.adr);
        printf("  Procedures:\n");

        bool at_function = false;
        

        // For every symbol record in the file descriptor record.
        for (auto symr : fdr.get_symrs(all_symrs)) {
            MDebug::ST type = symr.get_st();
            if (type == MDebug::ST_PROC || type == MDebug::ST_STATICPROC) {
                at_function = true;
                const char* name = fdr.get_string(mdata + hdrr.cbSsOffset, symr.iss);
                printf("    %s @ 0x%08X\n", name, symr.value);
                auto find_it = context.functions_by_name.find(name);
                if (find_it != context.functions_by_name.end()) {
                    printf("      in map: 0x%08lX\n", context.functions[find_it->second].vram);
                }
            }
            else if (type == MDebug::ST_END) {
                printf("      size: 0x%08lX\n", symr.value);
            }
        }
        
        printf("  Globals:\n");

        for (auto symr : fdr.get_symrs(all_symrs)) {
            MDebug::ST type = symr.get_st();
            if (type == MDebug::ST_GLOBAL) {
                const char* name = fdr.get_string(mdata + hdrr.cbSsOffset, symr.iss);
                printf("    %s @ 0x%08X\n", name, symr.value);
            }
        }

        // // Consider only procedures and symbols defined in this file
        // std::span<MDebug::AUX> auxs = fdr.get_auxs(all_auxs);
        // std::span<MDebug::PDR> pdrs = fdr.get_pdrs(all_pdrs);
        // std::span<MDebug::SYMR> symrs = fdr.get_symrs(all_symrs);

        // std::vector<std::pair<uint32_t,uint32_t>> bounds(pdrs.size());

        // // For each procedure record, determine which symbols belong to it
        // for (MDebug::PDR& pdr : pdrs) {
        //     auto res = pdr.sym_bounds(symrs, auxs);
        //     bounds.push_back(res);
        // }

        // // For each symbol defined in this file
        // for (uint32_t isym = 0; isym < symrs.size(); isym++) {
        //     MDebug::SYMR& symr = symrs[isym];

        //     // // Skip non-statics
        //     // if (symr.get_st() != MDebug::ST_STATIC && symr.get_st() != MDebug::ST_STATICPROC) {
        //     //     continue;
        //     // }

        //     // // Find the name of the PDR, if any, that contains this symbol. We computed the (ordered)
        //     // // symbol bounds above, bsearch for the PDR.
        //     // auto it = std::lower_bound(bounds.begin(), bounds.end(), isym,
        //     //     [](const auto& bound, uint32_t x) {
        //     //         return bound.second <= x;
        //     //     }
        //     // );
        //     // const char* pdr_name = nullptr;
        //     // if (it != bounds.end() && it->first <= isym && isym < it->second) {
        //     //     // The procedure name is the name of the first symbol
        //     //     pdr_name = fdr.get_string(mdata + hdrr.cbSsOffset, symrs[it->first].iss);
        //     // }

        //     // Get the name of the static symbol
        //     const char* sym_name = fdr.get_string(mdata + hdrr.cbSsOffset, symr.iss);

        //     // // Present info (TODO: plug into everything else)
        //     // std::printf("0x%08X %s (in %s, in %s(0x%08X))\n", symr.value, sym_name, pdr_name, fdr_name, fdr.adr);

        //     // Present info (TODO: plug into everything else)
        //     std::printf("0x%08X (0x%08X) %s (in %s(0x%08X))\n", symr.value, symr.bits, sym_name, fdr_name, fdr.adr);
        // }
    }
}
#endif

bool N64Recomp::MDebug::parse_mdebug(const N64Recomp::ElfParsingConfig& elf_config, const char* mdebug_section, uint32_t mdebug_offset, N64Recomp::Context& context, N64Recomp::DataSymbolMap& data_syms) {
    MDebugInfo mdebug_info{ elf_config, mdebug_section, mdebug_offset };

    if (!mdebug_info.populate_context(elf_config, context, data_syms)) {
        return false;
    }

    return true;
}
