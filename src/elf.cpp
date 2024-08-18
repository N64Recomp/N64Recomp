#include "fmt/format.h"
// #include "fmt/ostream.h"

#include "n64recomp.h"
#include "elfio/elfio.hpp"

bool read_symbols(N64Recomp::Context& context, const ELFIO::elfio& elf_file, ELFIO::section* symtab_section, const N64Recomp::ElfParsingConfig& elf_config, bool dumping_context, std::unordered_map<uint16_t, std::vector<N64Recomp::DataSymbol>>& data_syms) {
    bool found_entrypoint_func = false;
    ELFIO::symbol_section_accessor symbols{ elf_file, symtab_section };

    std::unordered_map<uint16_t, uint16_t> bss_section_to_target_section{};

    // Create a mapping of bss section to the corresponding non-bss section. This is only used when dumping context in order
    // for patches and mods to correctly relocate symbols in bss. This mapping only matters for relocatable sections.
    if (dumping_context) {
        // Process bss and reloc sections
        for (size_t cur_section_index = 0; cur_section_index < context.sections.size(); cur_section_index++) {
            const N64Recomp::Section& cur_section = context.sections[cur_section_index];
            // Check if a bss section was found that corresponds with this section.
            if (cur_section.bss_section_index != (uint16_t)-1) {
                bss_section_to_target_section[cur_section.bss_section_index] = cur_section_index;
            }
        }
    }

    for (int sym_index = 0; sym_index < symbols.get_symbols_num(); sym_index++) {
        std::string   name;
        ELFIO::Elf64_Addr    value;
        ELFIO::Elf_Xword     size;
        unsigned char bind;
        unsigned char type;
        ELFIO::Elf_Half      section_index;
        unsigned char other;
        bool ignored = false;
        bool reimplemented = false;
        bool recorded_symbol = false;

        // Read symbol properties
        symbols.get_symbol(sym_index, name, value, size, bind, type,
            section_index, other);

        if (section_index == ELFIO::SHN_ABS && elf_config.use_absolute_symbols) {
            uint32_t vram = static_cast<uint32_t>(value);
            context.functions_by_vram[vram].push_back(context.functions.size());

            context.functions.emplace_back(
                vram,
                0,
                std::vector<uint32_t>{},
                std::move(name),
                0,
                true,
                reimplemented,
                false
            );
            continue;
        }

        if (section_index < context.sections.size()) {        
            // Check if this symbol is the entrypoint
            if (elf_config.has_entrypoint && value == elf_config.entrypoint_address && type == ELFIO::STT_FUNC) {
                if (found_entrypoint_func) {
                    fmt::print(stderr, "Ambiguous entrypoint: {}\n", name);
                    return false;
                }
                found_entrypoint_func = true;
                fmt::print("Found entrypoint, original name: {}\n", name);
                size = 0x50; // dummy size for entrypoints, should cover them all
                name = "recomp_entrypoint";
            }

            // Check if this symbol has a size override
            auto size_find = elf_config.manually_sized_funcs.find(name);
            if (size_find != elf_config.manually_sized_funcs.end()) {
                size = size_find->second;
                type = ELFIO::STT_FUNC;
            }

            if (!dumping_context) {
                if (N64Recomp::reimplemented_funcs.contains(name)) {
                    reimplemented = true;
                    name = name + "_recomp";
                    ignored = true;
                } else if (N64Recomp::ignored_funcs.contains(name)) {
                    name = name + "_recomp";
                    ignored = true;
                }
            }

            auto& section = context.sections[section_index];

            // Check if this symbol is a function or has no type (like a regular glabel would)
            // Symbols with no type have a dummy entry created so that their symbol can be looked up for function calls
            if (ignored || type == ELFIO::STT_FUNC || type == ELFIO::STT_NOTYPE || type == ELFIO::STT_OBJECT) {
                if (!dumping_context) {
                    if (N64Recomp::renamed_funcs.contains(name)) {
                        name = name + "_recomp";
                        ignored = false;
                    }
                }

                if (section_index < context.sections.size()) {
                    auto section_offset = value - elf_file.sections[section_index]->get_address();
                    const uint32_t* words = reinterpret_cast<const uint32_t*>(elf_file.sections[section_index]->get_data() + section_offset);
                    uint32_t vram = static_cast<uint32_t>(value);
                    uint32_t num_instructions = type == ELFIO::STT_FUNC ? size / 4 : 0;
                    uint32_t rom_address = static_cast<uint32_t>(section_offset + section.rom_addr);

                    section.function_addrs.push_back(vram);
                    context.functions_by_vram[vram].push_back(context.functions.size());

                    // Find the entrypoint by rom address in case it doesn't have vram as its value
                    if (elf_config.has_entrypoint && rom_address == 0x1000 && type == ELFIO::STT_FUNC) {
                        vram = elf_config.entrypoint_address;
                        found_entrypoint_func = true;
                        name = "recomp_entrypoint";
                        if (size == 0) {
                            num_instructions = 0x50 / 4;
                        }
                    }

                    // Suffix local symbols to prevent name conflicts.
                    if (bind == ELFIO::STB_LOCAL) {
                        name = fmt::format("{}_{:08X}", name, rom_address);
                    }
                    
                    if (num_instructions > 0) {
                        context.section_functions[section_index].push_back(context.functions.size());            
                        recorded_symbol = true;
                    }
                    context.functions_by_name[name] = context.functions.size();

                    std::vector<uint32_t> insn_words(num_instructions);
                    insn_words.assign(words, words + num_instructions);

                    context.functions.emplace_back(
                        vram,
                        rom_address,
                        std::move(insn_words),
                        name,
                        section_index,
                        ignored,
                        reimplemented
                    );
                } else {
                    // TODO is this case needed anymore?
                    uint32_t vram = static_cast<uint32_t>(value);
                    section.function_addrs.push_back(vram);
                    context.functions_by_vram[vram].push_back(context.functions.size());
                    context.functions.emplace_back(
                        vram,
                        0,
                        std::vector<uint32_t>{},
                        name,
                        section_index,
                        ignored,
                        reimplemented
                    );
                }
            }
        }

        // The symbol wasn't detected as a function, so add it to the data symbols if the context is being dumped.
        if (!recorded_symbol && dumping_context && !name.empty()) {
            uint32_t vram = static_cast<uint32_t>(value);

            // Place this symbol in the absolute symbol list if it's in the absolute section.
            uint16_t target_section_index = section_index;
            if (section_index == ELFIO::SHN_ABS) {
                target_section_index = N64Recomp::SectionAbsolute;
            }
            else if (section_index >= context.sections.size()) {
                fmt::print("Symbol \"{}\" not in a valid section ({})\n", name, section_index);
            }

            // Move this symbol into the corresponding non-bss section if it's in a bss section.
            auto find_bss_it = bss_section_to_target_section.find(target_section_index);
            if (find_bss_it != bss_section_to_target_section.end()) {
                target_section_index = find_bss_it->second;
            }

            data_syms[target_section_index].emplace_back(
                vram,
                std::move(name)
            );
        }
    }

    return found_entrypoint_func;
}

struct SegmentEntry {
    ELFIO::Elf64_Off data_offset;
    ELFIO::Elf64_Addr physical_address;
    ELFIO::Elf_Xword memory_size;
};

std::optional<size_t> get_segment(const std::vector<SegmentEntry>& segments, ELFIO::Elf_Xword section_size, ELFIO::Elf64_Off section_offset) {
    // A linear search is safest even if the segment list is sorted, as there may be overlapping segments
    for (size_t i = 0; i < segments.size(); i++) {
        const auto& segment = segments[i];

        // Check that the section's data in the elf file is within bounds of the segment's data
        if (section_offset >= segment.data_offset && section_offset + section_size <= segment.data_offset + segment.memory_size) {
            return i;
        }
    }

    return std::nullopt;
}

ELFIO::section* read_sections(N64Recomp::Context& context, const N64Recomp::ElfParsingConfig& elf_config, const ELFIO::elfio& elf_file) {
    ELFIO::section* symtab_section = nullptr;
    std::vector<SegmentEntry> segments{};
    segments.resize(elf_file.segments.size());
    bool has_reference_symbols = context.has_reference_symbols();

    // Copy the data for each segment into the segment entry list
    for (size_t segment_index = 0; segment_index < elf_file.segments.size(); segment_index++) {
        const auto& segment = *elf_file.segments[segment_index];
        segments[segment_index].data_offset = segment.get_offset();
        segments[segment_index].physical_address = segment.get_physical_address();
        segments[segment_index].memory_size = segment.get_file_size();
    }

    //// Sort the segments by physical address
    //std::sort(segments.begin(), segments.end(),
    //    [](const SegmentEntry& lhs, const SegmentEntry& rhs) {
    //        return lhs.data_offset < rhs.data_offset;
    //    }
    //);

    std::unordered_map<std::string, ELFIO::section*> reloc_sections_by_name;
    std::unordered_map<std::string, ELFIO::section*> bss_sections_by_name;

    // Iterate over every section to record rom addresses and find the symbol table
    for (const auto& section : elf_file.sections) {
        auto& section_out = context.sections[section->get_index()];
        //fmt::print("  {}: {} @ 0x{:08X}, 0x{:08X}\n", section->get_index(), section->get_name(), section->get_address(), context.rom.size());
        // Set the rom address of this section to the current accumulated ROM size
        section_out.ram_addr = section->get_address();
        section_out.size = section->get_size();
        ELFIO::Elf_Word type = section->get_type();
        std::string section_name = section->get_name();

        // Check if this section is the symbol table and record it if so
        if (type == ELFIO::SHT_SYMTAB) {
            symtab_section = section.get();
        }

        if (elf_config.all_sections_relocatable || elf_config.relocatable_sections.contains(section_name)) {
            section_out.relocatable = true;
        }
        
        // Check if this section is a reloc section
        if (type == ELFIO::SHT_REL) {
            // If it is, determine the name of the section it relocates
            if (!section_name.starts_with(".rel")) {
                fmt::print(stderr, "Could not determine corresponding section for reloc section {}\n", section_name.c_str());
                return nullptr;
            }
            
            std::string reloc_target_section = section_name.substr(strlen(".rel"));

            // If this reloc section is for a section that has been marked as relocatable, record it in the reloc section lookup.
            // Alternatively, if this recompilation uses reference symbols then record all reloc sections.
            bool section_is_relocatable = elf_config.all_sections_relocatable || elf_config.relocatable_sections.contains(reloc_target_section);
            if (has_reference_symbols || section_is_relocatable) {
                reloc_sections_by_name[reloc_target_section] = section.get();
            }
        }

        // If the section is bss (SHT_NOBITS) and ends with the bss suffix, add it to the bss section map
        if (type == ELFIO::SHT_NOBITS && section_name.ends_with(elf_config.bss_section_suffix)) {
            std::string bss_target_section = section_name.substr(0, section_name.size() - elf_config.bss_section_suffix.size());

            // If this bss section is for a section that has been marked as relocatable, record it in the reloc section lookup
            if (elf_config.all_sections_relocatable || elf_config.relocatable_sections.contains(bss_target_section)) {
                bss_sections_by_name[bss_target_section] = section.get();
            }
        }

        // If this section isn't bss (SHT_NOBITS) and ends up in the rom (SHF_ALLOC), 
        // find this section's rom address and copy it into the rom
        if (type != ELFIO::SHT_NOBITS && section->get_flags() & ELFIO::SHF_ALLOC && section->get_size() != 0) {
            //// Find the segment this section is in to determine the physical (rom) address of the section
            //auto segment_it = std::upper_bound(segments.begin(), segments.end(), section->get_offset(),
            //    [](ELFIO::Elf64_Off section_offset, const SegmentEntry& segment) {
            //        return section_offset < segment.data_offset;
            //    }
            //);
            //if (segment_it == segments.begin()) {
            //    fmt::print(stderr, "Could not find segment that section {} belongs to!\n", section_name.c_str());
            //    return nullptr;
            //}
            //// Upper bound returns the iterator after the element we're looking for, so rewind by one
            //// This is safe because we checked if segment_it was segments.begin() already, which is the minimum value it could be
            //const SegmentEntry& segment = *(segment_it - 1);
            //// Check to be sure that the section is actually in this segment
            //if (section->get_offset() >= segment.data_offset + segment.memory_size) {
            //    fmt::print(stderr, "Section {} out of range of segment at offset 0x{:08X}\n", section_name.c_str(), segment.data_offset);
            //    return nullptr;
            //}
            std::optional<size_t> segment_index = get_segment(segments, section_out.size, section->get_offset());
            if (!segment_index.has_value()) {
                fmt::print(stderr, "Could not find segment that section {} belongs to!\n", section_name.c_str());
                return nullptr;
            }
            const SegmentEntry& segment = segments[segment_index.value()];
            // Calculate the rom address based on this section's offset into the segment and the segment's rom address
            section_out.rom_addr = segment.physical_address + (section->get_offset() - segment.data_offset);
            // Resize the output rom if needed to fit this section
            size_t required_rom_size = section_out.rom_addr + section_out.size;
            if (required_rom_size > context.rom.size()) {
                context.rom.resize(required_rom_size);
            }
            // Copy this section's data into the rom
            std::copy(section->get_data(), section->get_data() + section->get_size(), &context.rom[section_out.rom_addr]);
        } else {
            // Otherwise mark this section as having an invalid rom address
            section_out.rom_addr = (uint32_t)-1;
        }
        // Check if this section is marked as executable, which means it has code in it
        if (section->get_flags() & ELFIO::SHF_EXECINSTR) {
            section_out.executable = true;
        }
        section_out.name = section_name;
    }

    if (symtab_section == nullptr) {
        fmt::print(stderr, "No symtab section found\n");
        return nullptr;
    }

    ELFIO::symbol_section_accessor symbol_accessor{ elf_file, symtab_section };
    auto num_syms = symbol_accessor.get_symbols_num();

    // TODO make sure that a reloc section was found for every section marked as relocatable

    // Process bss and reloc sections
    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        N64Recomp::Section& section_out = context.sections[section_index];
        // Check if a bss section was found that corresponds with this section
        auto bss_find = bss_sections_by_name.find(section_out.name);
        if (bss_find != bss_sections_by_name.end()) {
            section_out.bss_section_index = bss_find->second->get_index();
            section_out.bss_size = bss_find->second->get_size();
        }

        if (context.has_reference_symbols() || section_out.relocatable) {
            // Check if a reloc section was found that corresponds with this section
            auto reloc_find = reloc_sections_by_name.find(section_out.name);
            if (reloc_find != reloc_sections_by_name.end()) {
                // Create an accessor for the reloc section
                ELFIO::relocation_section_accessor rel_accessor{ elf_file, reloc_find->second };
                // Allocate space for the relocs in this section
                section_out.relocs.resize(rel_accessor.get_entries_num());
                // Track whether the previous reloc was a HI16 and its previous full_immediate
                bool prev_hi = false;
                // Track whether the previous reloc was a LO16
                bool prev_lo = false;
                uint32_t prev_hi_immediate = 0;
                uint32_t prev_hi_symbol = std::numeric_limits<uint32_t>::max();

                for (size_t i = 0; i < section_out.relocs.size(); i++) {
                    // Get the current reloc
                    ELFIO::Elf64_Addr rel_offset;
                    ELFIO::Elf_Word rel_symbol;
                    unsigned int rel_type;
                    ELFIO::Elf_Sxword bad_rel_addend; // Addends aren't encoded in the reloc, so ignore this one
                    rel_accessor.get_entry(i, rel_offset, rel_symbol, rel_type, bad_rel_addend);

                    N64Recomp::Reloc& reloc_out = section_out.relocs[i];

                    // Get the real full_immediate by extracting the immediate from the instruction
                    uint32_t reloc_rom_addr = section_out.rom_addr + rel_offset - section_out.ram_addr;
                    uint32_t reloc_rom_word = byteswap(*reinterpret_cast<const uint32_t*>(context.rom.data() + reloc_rom_addr));
                    //context.rom section_out.rom_addr;

                    reloc_out.address = rel_offset;
                    reloc_out.symbol_index = rel_symbol;
                    reloc_out.type = static_cast<N64Recomp::RelocType>(rel_type);

                    std::string       rel_symbol_name;
                    ELFIO::Elf64_Addr rel_symbol_value;
                    ELFIO::Elf_Xword  rel_symbol_size;
                    unsigned char     rel_symbol_bind;
                    unsigned char     rel_symbol_type;
                    ELFIO::Elf_Half   rel_symbol_section_index;
                    unsigned char     rel_symbol_other;

                    bool found_rel_symbol = symbol_accessor.get_symbol(
                        rel_symbol, rel_symbol_name, rel_symbol_value, rel_symbol_size, rel_symbol_bind, rel_symbol_type, rel_symbol_section_index, rel_symbol_other);

                    uint32_t rel_section_vram = 0;
                    uint32_t rel_symbol_offset = 0;

                    // Check if the symbol is undefined and to know whether to look for it in the reference symbols.
                    if (rel_symbol_section_index == ELFIO::SHN_UNDEF) {
                        // Undefined sym, check the reference symbols.
                        N64Recomp::SymbolReference sym_ref;
                        if (!context.find_reference_symbol(rel_symbol_name, sym_ref)) {
                            fmt::print(stderr, "Undefined symbol: {}, not found in input or reference symbols!\n",
                                rel_symbol_name);
                            return nullptr;
                        }
                        
                        reloc_out.reference_symbol = true;
                        // Replace the reloc's symbol index with the index into the reference symbol array.
                        rel_section_vram = 0;
                        reloc_out.target_section = sym_ref.section_index;
                        reloc_out.symbol_index = sym_ref.symbol_index;
                        const auto& reference_symbol = context.get_reference_symbol(reloc_out.target_section, reloc_out.symbol_index);
                        rel_symbol_offset = reference_symbol.section_offset;

                        bool target_section_relocatable = context.is_reference_section_relocatable(reloc_out.target_section);

                        if (reloc_out.type == N64Recomp::RelocType::R_MIPS_32 && target_section_relocatable) {
                            fmt::print(stderr, "Cannot reference {} in a statically initialized variable as it's defined in a relocatable section!\n",
                                rel_symbol_name);
                            return nullptr;
                        }
                    }
                    else {
                        reloc_out.reference_symbol = false;
                        reloc_out.target_section = rel_symbol_section_index;
                        // Handle special sections.
                        if (rel_symbol_section_index >= context.sections.size()) {
                            switch (rel_symbol_section_index) {
                            case ELFIO::SHN_ABS:
                                rel_section_vram = 0;
                                break;
                            default:
                                fmt::print(stderr, "Reloc {} references symbol {} which is in an unknown section 0x{:04X}!\n",
                                    i, rel_symbol_name, rel_symbol_section_index);
                                return nullptr;
                            }
                        }
                        else {
                            rel_section_vram = context.sections[rel_symbol_section_index].ram_addr;
                        }
                    }

                    // Reloc pairing, see MIPS System V ABI documentation page 4-18 (https://refspecs.linuxfoundation.org/elf/mipsabi.pdf)
                    if (reloc_out.type == N64Recomp::RelocType::R_MIPS_LO16) {
                        uint32_t rel_immediate = reloc_rom_word & 0xFFFF;
                        uint32_t full_immediate = (prev_hi_immediate << 16) + (int16_t)rel_immediate;
                        reloc_out.target_section_offset = full_immediate + rel_symbol_offset - rel_section_vram;
                        if (prev_hi) {
                            if (prev_hi_symbol != rel_symbol) {
                                fmt::print(stderr, "Paired HI16 and LO16 relocations have different symbols\n"
                                                    "  LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                    i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                return nullptr;
                            }

                            // Set the previous HI16 relocs' relocated address.
                            section_out.relocs[i - 1].target_section_offset = reloc_out.target_section_offset;
                        }
                        else {
                            // Orphaned LO16 reloc warnings.
                            if (elf_config.unpaired_lo16_warnings) {
                                if (prev_lo) {
                                    // Don't warn if multiple LO16 in a row reference the same symbol, as some linkers will use this behavior.
                                    if (prev_hi_symbol != rel_symbol) {
                                        fmt::print(stderr, "[WARN] LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X} follows LO16 with different symbol\n",
                                            i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                    }
                                }
                                else {
                                    fmt::print(stderr, "[WARN] Unpaired LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                        i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                }
                            }
                            // Even though this is an orphaned LO16 reloc, the previous calculation for the addend still follows the MIPS System V ABI documentation:
                            // "R_MIPS_LO16 entries without an R_MIPS_HI16 entry immediately preceding are orphaned and the previously defined
                            // R_MIPS_HI16 is used for computing the addend."
                            // Therefore, nothing needs to be done to the section_offset member.
                        }
                        prev_lo = true;
                    } else {
                        if (prev_hi) {
                            // This is an invalid elf as the MIPS System V ABI documentation states:
                            // "Each relocation type of R_MIPS_HI16 must have an associated R_MIPS_LO16 entry
                            // immediately following it in the list of relocations."
                            fmt::print(stderr, "Unpaired HI16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                i - 1, section_out.name, section_out.relocs[i - 1].symbol_index, section_out.relocs[i - 1].address);
                            return nullptr;
                        }
                        prev_lo = false;
                    }

                    if (reloc_out.type == N64Recomp::RelocType::R_MIPS_HI16) {
                        uint32_t rel_immediate = reloc_rom_word & 0xFFFF;
                        prev_hi = true;
                        prev_hi_immediate = rel_immediate;
                        prev_hi_symbol = rel_symbol;
                    } else {
                        prev_hi = false;
                    }

                    if (reloc_out.type == N64Recomp::RelocType::R_MIPS_32) {
                        // The reloc addend is just the existing word before relocation, so the section offset can just be the symbol's section offset.
                        // Incorporating the addend will be handled at load-time.
                        reloc_out.target_section_offset = rel_symbol_offset;
                        // TODO set section_out.has_mips32_relocs to true if this section should emit its mips32 relocs (mainly for TLB mapping).

                        if (reloc_out.reference_symbol) {
                            uint32_t reloc_target_section_addr = context.get_reference_section_vram(reloc_out.target_section);
                            // Patch the word in the ROM to incorporate the symbol's value.
                            uint32_t updated_reloc_word = reloc_rom_word + reloc_target_section_addr + reloc_out.target_section_offset;
                            *reinterpret_cast<uint32_t*>(context.rom.data() + reloc_rom_addr) = byteswap(updated_reloc_word);
                        }
                    }

                    if (reloc_out.type == N64Recomp::RelocType::R_MIPS_26) {
                        uint32_t rel_immediate = (reloc_rom_word & 0x3FFFFFF) << 2;
                        reloc_out.target_section_offset = rel_immediate + rel_symbol_offset;
                    }
                }
            }

            // Sort this section's relocs by address, which allows for binary searching and more efficient iteration during recompilation.
            // This is safe to do as the entire full_immediate in present in relocs due to the pairing that was done earlier, so the HI16 does not
            // need to directly preceed the matching LO16 anymore.
            std::sort(section_out.relocs.begin(), section_out.relocs.end(), 
                [](const N64Recomp::Reloc& a, const N64Recomp::Reloc& b) {
                    return a.address < b.address;
                }
            );
        }
    }

    return symtab_section;
}

static void setup_context_for_elf(N64Recomp::Context& context, const ELFIO::elfio& elf_file) {
    context.sections.resize(elf_file.sections.size());
    context.section_functions.resize(elf_file.sections.size());
    context.functions.reserve(1024);
    context.functions_by_vram.reserve(context.functions.capacity());
    context.functions_by_name.reserve(context.functions.capacity());
    context.rom.reserve(8 * 1024 * 1024);
}

bool N64Recomp::Context::from_elf_file(const std::filesystem::path& elf_file_path, Context& out, const ElfParsingConfig& elf_config, bool for_dumping_context, DataSymbolMap& data_syms_out, bool& found_entrypoint_out) {
    ELFIO::elfio elf_file;

    if (!elf_file.load(elf_file_path.string())) {
        fmt::print("Elf file not found\n");
        return false;
    }

    if (elf_file.get_class() != ELFIO::ELFCLASS32) {
        fmt::print("Incorrect elf class\n");
        return false;
    }

    if (elf_file.get_encoding() != ELFIO::ELFDATA2MSB) {
        fmt::print("Incorrect endianness\n");
        return false;
    }

    setup_context_for_elf(out, elf_file);

    // Read all of the sections in the elf and look for the symbol table section
    ELFIO::section* symtab_section = read_sections(out, elf_config, elf_file);

    // If no symbol table was found then exit
    if (symtab_section == nullptr) {
        return false;
    }

    // Read all of the symbols in the elf and look for the entrypoint function
    found_entrypoint_out = read_symbols(out, elf_file, symtab_section, elf_config, for_dumping_context, data_syms_out);

    return true;
}
