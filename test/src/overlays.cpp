#include <unordered_map>
#include <algorithm>
#include <vector>
#include "recomp.h"
#include "../funcs/recomp_overlays.inl"

constexpr size_t num_sections = ARRLEN(section_table);

// SectionTableEntry sections[] defined in recomp_overlays.inl

struct LoadedSection {
    int32_t loaded_ram_addr;
    size_t section_table_index;

    bool operator<(const LoadedSection& rhs) {
        return loaded_ram_addr < rhs.loaded_ram_addr;
    }
};

std::vector<LoadedSection> loaded_sections{};
std::unordered_map<int32_t, recomp_func_t*> func_map{};

void load_overlay(size_t section_table_index, int32_t ram) {
    const SectionTableEntry& section = section_table[section_table_index];
    for (size_t function_index = 0; function_index < section.num_funcs; function_index++) {
        const FuncEntry& func = section.funcs[function_index];
        func_map[ram + func.offset] = func.func;
    }
    loaded_sections.emplace_back(ram, section_table_index);
}

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size) {
    // Search for the first section that's included in the loaded rom range
    // Sections were sorted by `init_overlays` so we can use the bounds functions
    auto lower = std::lower_bound(&section_table[0], &section_table[num_sections], rom, 
        [](const SectionTableEntry& entry, uint32_t addr) {
            return entry.rom_addr < addr;
        }
    );
    auto upper = std::upper_bound(&section_table[0], &section_table[num_sections], (uint32_t)(rom + size),
        [](uint32_t addr, const SectionTableEntry& entry) {
            return addr < entry.size + entry.rom_addr;
        }
    );
    // Load the overlays that were found
    for (auto it = lower; it != upper; ++it) {
        load_overlay(std::distance(&section_table[0], it), it->rom_addr - rom + ram_addr);
    }
}

extern "C" void unload_overlays(int32_t ram_addr, uint32_t size) {
    for (auto it = loaded_sections.begin(); it != loaded_sections.end();) {
        const auto& section = section_table[it->section_table_index];

        // Check if the unloaded region overlaps with the loaded section
        if (ram_addr < (it->loaded_ram_addr + section.size) && (ram_addr + size) >= it->loaded_ram_addr) {
            // Check if the section isn't entirely in the loaded region
            if (ram_addr > it->loaded_ram_addr || (ram_addr + size) < (it->loaded_ram_addr + section.size)) {
                fprintf(stderr,
                    "Cannot partially unload section\n"
                    "  rom: 0x%08X size: 0x%08X loaded_addr: 0x%08X\n"
                    "  unloaded_ram: 0x%08X unloaded_size : 0x%08X\n",
                        section.rom_addr, section.size, it->loaded_ram_addr, ram_addr, size);
                std::exit(EXIT_FAILURE);
            }
            // Determine where each function was loaded to and remove that entry from the function map
            for (size_t func_index = 0; func_index < section.num_funcs; func_index++) {
                const auto& func = section.funcs[func_index];
                uint32_t func_address = func.offset + it->loaded_ram_addr;
                func_map.erase(func_address);
            }
            // Remove the section from the loaded section map
            it = loaded_sections.erase(it);
            // Skip incrementing the iterator
            continue;
        }
        ++it;
    }
}

void init_overlays() {
    // Sort the executable sections by rom address
    std::sort(&section_table[0], &section_table[num_sections],
        [](const SectionTableEntry& a, const SectionTableEntry& b) {
            return a.rom_addr < b.rom_addr;
        }
    );
}

extern "C" recomp_func_t * get_function(int32_t addr) {
    auto func_find = func_map.find(addr);
    if (func_find == func_map.end()) {
        fprintf(stderr, "Failed to find function at 0x%08X\n", addr);
        std::exit(EXIT_FAILURE);
    }
    return func_find->second;
}

