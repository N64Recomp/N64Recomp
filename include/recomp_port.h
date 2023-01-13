#ifndef __RECOMP_PORT__
#define __RECOMP_PORT__

#include <span>
#include <string_view>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <span>
#include "elfio/elfio.hpp"

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

namespace RecompPort {

    struct JumpTable {
        uint32_t vram;
        uint32_t addend_reg;
        uint32_t rom;
        uint32_t lw_vram;
        uint32_t addu_vram;
        uint32_t jr_vram;
        std::vector<uint32_t> entries;
    };

    struct AbsoluteJump {
        uint32_t jump_target;
        uint32_t instruction_vram;
    };

    struct Function {
        uint32_t vram;
        uint32_t rom;
        const std::span<const uint32_t> words;
        std::string name;
        ELFIO::Elf_Half section_index;
        bool ignored;
        bool reimplemented;
    };

    struct Section {
        ELFIO::Elf_Xword rom_addr;
        ELFIO::Elf64_Addr ram_addr;
        ELFIO::Elf_Xword size;
        std::vector<uint32_t> function_addrs;
        std::string name;
        bool executable;
    };

    struct FunctionStats {
        std::vector<JumpTable> jump_tables;
        std::vector<AbsoluteJump> absolute_jumps;
    };

    struct Context {
        // ROM address of each section
        std::vector<Section> sections;
        std::vector<Function> functions;
        std::unordered_map<uint32_t, std::vector<size_t>> functions_by_vram;
        std::vector<uint8_t> rom;
        // A list of the list of each function (by index in `functions`) in a given section
        std::vector<std::vector<size_t>> section_functions;
        int executable_section_count;

        Context(const ELFIO::elfio& elf_file) {
            sections.resize(elf_file.sections.size());
            section_functions.resize(elf_file.sections.size());
            functions.reserve(1024);
            functions_by_vram.reserve(1024);
            rom.reserve(8 * 1024 * 1024);
            executable_section_count = 0;
        }
    };

    bool analyze_function(const Context& context, const Function& function, const std::vector<rabbitizer::InstructionCpu>& instructions, FunctionStats& stats);
    bool recompile_function(const Context& context, const Function& func, std::string_view output_path, std::span<std::vector<uint32_t>> static_funcs);
}

#endif
