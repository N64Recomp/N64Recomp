#include "n64recomp.h"

struct FileHeader {
    char magic[8]; // N64RSYMS
    uint32_t version;
};

struct FileSubHeaderV1 {
    uint32_t num_sections;
};

struct SectionHeaderV1 {
    uint32_t file_offset;
    uint32_t vram;
    uint32_t original_vrom; // 0 if this is a new section
    uint32_t rom_size;
    uint32_t bss_size;
    uint32_t num_funcs;
    uint32_t num_relocs;
    uint32_t num_replacements;
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

    ret.sections.resize(num_sections);
    mod_context.section_info.resize(num_sections);
    for (size_t section_index = 0; section_index < num_sections; section_index++) {
        const SectionHeaderV1* section_header = reinterpret_data<SectionHeaderV1>(data, offset);
        if (section_header == nullptr) {
            return false;
        }

        N64Recomp::Section& cur_section = ret.sections[section_index];
        N64Recomp::ModSectionInfo& cur_mod_section = mod_context.section_info[section_index];

        cur_section.rom_addr = section_header->file_offset;
        cur_section.ram_addr = section_header->vram;
        cur_section.size = section_header->rom_size;
        cur_section.bss_size = section_header->bss_size;
        cur_section.name = "mod_section_" + std::to_string(section_index);
        cur_mod_section.original_rom_addr = section_header->original_vrom;
        uint32_t num_funcs = section_header->num_funcs;
        uint32_t num_relocs = section_header->num_relocs;
        uint32_t num_replacements = section_header->num_replacements;


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

        const ReplacementV1* replacements = reinterpret_data<ReplacementV1>(data, offset, num_replacements);
        if (replacements == nullptr) {
            printf("Failed to read replacements (count: %d)\n", num_replacements);
            return false;
        }

        size_t start_func_index = ret.functions.size();
        ret.functions.resize(ret.functions.size() + num_funcs);
        cur_section.relocs.resize(num_relocs);
        cur_mod_section.replacements.resize(num_replacements);

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
            if (target_section_vrom == (uint32_t)-1) {
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

        for (size_t replacement_index = 0; replacement_index < num_replacements; replacement_index++) {
            N64Recomp::FunctionReplacement& cur_replacement = cur_mod_section.replacements[replacement_index];
            
            cur_replacement.func_index = replacements[replacement_index].func_index;
            cur_replacement.original_vram = replacements[replacement_index].original_vram;
            cur_replacement.flags = static_cast<N64Recomp::ReplacementFlags>(replacements[replacement_index].flags);
        }
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
