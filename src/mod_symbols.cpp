#include "n64recomp.h"

struct FileHeader {
    char magic[8]; // N64RSYMS
    uint32_t version;
};

struct FileSubHeaderV1 {
    uint32_t num_sections;
    uint32_t num_replacements;
};

struct SectionHeaderV1 {
    uint32_t file_offset;
    uint32_t vram;
    uint32_t rom_size;
    uint32_t bss_size;
    uint32_t num_funcs;
    uint32_t num_relocs;
};

struct FuncV1 {
    uint32_t section_offset;
    uint32_t size;
};

struct RelocV1 {
    uint32_t section_offset;
    uint32_t type;
    uint32_t target_section_offset;
    uint32_t target_section_vrom; // 0 means current section
};

struct ReplacementV1 {
    uint32_t func_index;
    uint32_t original_section_vrom;
    uint32_t original_vram;
    uint32_t flags; // force
};

template <typename T>
const T* reinterpret_data(std::span<const char> data, size_t& offset, size_t count = 1) {
    if (offset + (sizeof(T) * count) > data.size()) {
        return nullptr;
    }

    size_t original_offset = offset;
    offset += sizeof(T) * count;
    return reinterpret_cast<const T*>(data.data() + original_offset);
}

bool check_magic(const FileHeader* header) {
    static const char good_magic[] = {'N','6','4','R','S','Y','M','S'};
    static_assert(sizeof(good_magic) == sizeof(FileHeader::magic));

    return memcmp(header->magic, good_magic, sizeof(good_magic)) == 0;
}

bool parse_v1(std::span<const char> data, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, N64Recomp::Context& ret, N64Recomp::ModContext& mod_context) {
    size_t offset = sizeof(FileHeader);
    const FileSubHeaderV1* subheader = reinterpret_data<FileSubHeaderV1>(data, offset);
    if (subheader == nullptr) {
        return false;
    }

    size_t num_sections = subheader->num_sections;
    size_t num_replacements = subheader->num_replacements;

    ret.sections.resize(num_sections);
    mod_context.replacements.resize(num_replacements);
    for (size_t section_index = 0; section_index < num_sections; section_index++) {
        const SectionHeaderV1* section_header = reinterpret_data<SectionHeaderV1>(data, offset);
        if (section_header == nullptr) {
            return false;
        }

        N64Recomp::Section& cur_section = ret.sections[section_index];

        cur_section.rom_addr = section_header->file_offset;
        cur_section.ram_addr = section_header->vram;
        cur_section.size = section_header->rom_size;
        cur_section.bss_size = section_header->bss_size;
        cur_section.name = "mod_section_" + std::to_string(section_index);
        uint32_t num_funcs = section_header->num_funcs;
        uint32_t num_relocs = section_header->num_relocs;


        const FuncV1* funcs = reinterpret_data<FuncV1>(data, offset, num_funcs);
        if (funcs == nullptr) {
            printf("Failed to read funcs (count: %d)\n", num_funcs);
            return false;
        }

        const RelocV1* relocs = reinterpret_data<RelocV1>(data, offset, num_relocs);
        if (relocs == nullptr) {
            printf("Failed to read relocs (count: %d)\n", num_relocs);
            return false;
        }

        size_t start_func_index = ret.functions.size();
        ret.functions.resize(ret.functions.size() + num_funcs);
        cur_section.relocs.resize(num_relocs);

        for (size_t func_index = 0; func_index < num_funcs; func_index++) {
            uint32_t func_rom_addr = cur_section.rom_addr + funcs[func_index].section_offset;
            if ((func_rom_addr & 0b11) != 0) {
                printf("Function %zu in section %zu file offset is not a multiple of 4\n", func_index, section_index);
                return false;
            }

            if ((funcs[func_index].size & 0b11) != 0) {
                printf("Function %zu in section %zu size is not a multiple of 4\n", func_index, section_index);
                return false;
            }

            N64Recomp::Function& cur_func = ret.functions[start_func_index + func_index];
            cur_func.vram = cur_section.ram_addr + funcs[func_index].section_offset;
            cur_func.rom = cur_section.rom_addr + funcs[func_index].section_offset;
            cur_func.words.resize(funcs[func_index].size / sizeof(uint32_t)); // Filled in later
            cur_func.name = "mod_func_" + std::to_string(start_func_index + func_index);
            cur_func.section_index = section_index;
        }

        for (size_t reloc_index = 0; reloc_index < num_relocs; reloc_index++) {
            N64Recomp::Reloc& cur_reloc = cur_section.relocs[reloc_index];
            cur_reloc.address = cur_section.ram_addr + relocs[reloc_index].section_offset;
            cur_reloc.type = static_cast<N64Recomp::RelocType>(relocs[reloc_index].type);
            cur_reloc.target_section_offset = relocs[reloc_index].target_section_offset;
            uint32_t target_section_vrom = relocs[reloc_index].target_section_vrom;
            if (target_section_vrom == 0) {
                cur_reloc.target_section = N64Recomp::SectionSelf;
            }
            else {
                // TODO lookup by section index by original vrom
                auto find_section_it = sections_by_vrom.find(target_section_vrom);
                if (find_section_it == sections_by_vrom.end()) {
                    printf("Reloc %zu in section %zu size has a target section vrom (%08X) that doesn't match any original section\n",
                        reloc_index, section_index, target_section_vrom);
                    return false;
                }
                cur_reloc.target_section = find_section_it->second;
            }
        }
    }

    const ReplacementV1* replacements = reinterpret_data<ReplacementV1>(data, offset, num_replacements);
    if (replacements == nullptr) {
        printf("Failed to read replacements (count: %zu)\n", num_replacements);
        return false;
    }

    for (size_t replacement_index = 0; replacement_index < num_replacements; replacement_index++) {
        N64Recomp::FunctionReplacement& cur_replacement = mod_context.replacements[replacement_index];

        cur_replacement.func_index = replacements[replacement_index].func_index;
        cur_replacement.original_section_vrom = replacements[replacement_index].original_section_vrom;
        cur_replacement.original_vram = replacements[replacement_index].original_vram;
        cur_replacement.flags = static_cast<N64Recomp::ReplacementFlags>(replacements[replacement_index].flags);
    }

    return offset == data.size();
}

N64Recomp::ModSymbolsError N64Recomp::parse_mod_symbols(std::span<const char> data, std::span<const uint8_t> binary, const std::unordered_map<uint32_t, uint16_t>& sections_by_vrom, Context& context_out, ModContext& mod_context_out) {
    size_t offset = 0;
    context_out = {};
    mod_context_out = {};
    const FileHeader* header = reinterpret_data<FileHeader>(data, offset);

    if (header == nullptr) {
        return ModSymbolsError::NotASymbolFile;
    }

    if (!check_magic(header)) {
        return ModSymbolsError::NotASymbolFile;
    }

    bool valid = false;

    switch (header->version) {
        case 1:
            valid = parse_v1(data, sections_by_vrom, context_out, mod_context_out);
            break;
        default:
            return ModSymbolsError::UnknownSymbolFileVersion;
    }

    if (!valid) {
        context_out = {};
        mod_context_out = {};
        return ModSymbolsError::CorruptSymbolFile;
    }

    // Fill in the words for each function.
    for (auto& cur_func : context_out.functions) {
        if (cur_func.rom + cur_func.words.size() * sizeof(cur_func.words[0]) > binary.size()) {
            context_out = {};
            mod_context_out = {};
            return ModSymbolsError::FunctionOutOfBounds;
        }
        const uint32_t* func_rom = reinterpret_cast<const uint32_t*>(binary.data() + cur_func.rom);
        for (size_t word_index = 0; word_index < cur_func.words.size(); word_index++) {
            cur_func.words[word_index] = func_rom[word_index];
        }
    }

    return ModSymbolsError::Good;
}

template <typename T>
void vec_put(std::vector<uint8_t>& vec, const T* data) {
    size_t start_size = vec.size();
    vec.resize(vec.size() + sizeof(T));
    memcpy(vec.data() + start_size, reinterpret_cast<const uint8_t*>(data), sizeof(T));
}

std::vector<uint8_t> N64Recomp::symbols_to_bin_v1(const N64Recomp::ModContext& mod_context) {
    std::vector<uint8_t> ret{};
    ret.reserve(1024);
    const N64Recomp::Context& context = mod_context.base_context;

    const static FileHeader header {
        .magic = {'N', '6', '4', 'R', 'S', 'Y', 'M', 'S'},
        .version = 1
    };

    vec_put(ret, &header);

    FileSubHeaderV1 sub_header {
        .num_sections = static_cast<uint32_t>(context.sections.size()),
        .num_replacements = static_cast<uint32_t>(mod_context.replacements.size()),
    };

    vec_put(ret, &sub_header);

    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        const Section& cur_section = context.sections[section_index];
        SectionHeaderV1 section_out {
            .file_offset = cur_section.rom_addr,
            .vram = cur_section.ram_addr,
            .rom_size = cur_section.size,
            .bss_size = cur_section.bss_size,
            .num_funcs = static_cast<uint32_t>(context.section_functions[section_index].size()),
            .num_relocs = static_cast<uint32_t>(cur_section.relocs.size())
        };

        vec_put(ret, &section_out);

        for (size_t func_index : context.section_functions[section_index]) {
            const Function& cur_func = context.functions[func_index];
            FuncV1 func_out {
                .section_offset = cur_func.vram - cur_section.ram_addr,
                .size = (uint32_t)(cur_func.words.size() * sizeof(cur_func.words[0])) 
            };

            vec_put(ret, &func_out);
        }

        for (const Reloc& cur_reloc : cur_section.relocs) {
            uint32_t target_section_vrom;
            if (cur_reloc.target_section == SectionSelf) {
                target_section_vrom = 0;
            }
            else if (cur_reloc.reference_symbol) {
                target_section_vrom = context.reference_sections[cur_reloc.target_section].rom_addr;
            }
            else {
                target_section_vrom = context.sections[cur_reloc.target_section].rom_addr;
            }
            RelocV1 reloc_out {
                .section_offset = cur_reloc.address - cur_section.ram_addr,
                .type = static_cast<uint32_t>(cur_reloc.type),
                .target_section_offset = cur_reloc.target_section_offset,
                .target_section_vrom = target_section_vrom
            };

            vec_put(ret, &reloc_out);
        }
    }

    for (const FunctionReplacement& cur_replacement : mod_context.replacements) {
        uint32_t flags = 0;
        if ((cur_replacement.flags & ReplacementFlags::Force) == ReplacementFlags::Force) {
            flags |= 0x1;
        }

        ReplacementV1 replacement_out {
            .func_index = cur_replacement.func_index,
            .original_section_vrom = cur_replacement.original_section_vrom,
            .original_vram = cur_replacement.original_vram,
            .flags = flags
        };

        vec_put(ret, &replacement_out);
    };

    return ret;
}
