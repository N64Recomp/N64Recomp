#ifndef __RECOMP_CONFIG_H__
#define __RECOMP_CONFIG_H__

#include <cstdint>
#include <filesystem>
#include <vector>

namespace N64Recomp {
    struct InstructionPatch {
        std::string func_name;
        int32_t vram;
        uint32_t value;
    };

    struct FunctionHook {
        std::string func_name;
        int32_t before_vram;
        std::string text;
    };

    struct FunctionSize {
        std::string func_name;
        uint32_t size_bytes;

        FunctionSize(const std::string& func_name, uint32_t size_bytes) : func_name(std::move(func_name)), size_bytes(size_bytes) {}
    };

    struct ManualFunction {
        std::string func_name;
        std::string section_name;
        uint32_t vram;
        uint32_t size;

        ManualFunction(const std::string& func_name, std::string section_name, uint32_t vram, uint32_t size) : func_name(std::move(func_name)), section_name(std::move(section_name)), vram(vram), size(size) {}
    };

    struct Config {
        int32_t entrypoint;
        int32_t functions_per_output_file;
        bool has_entrypoint;
        bool uses_mips3_float_mode;
        bool single_file_output;
        bool use_absolute_symbols;
        bool unpaired_lo16_warnings;
        bool allow_exports;
        bool strict_patch_mode;
        std::filesystem::path elf_path;
        std::filesystem::path symbols_file_path;
        std::filesystem::path func_reference_syms_file_path;
        std::vector<std::filesystem::path> data_reference_syms_file_paths;
        std::filesystem::path rom_file_path;
        std::filesystem::path output_func_path;
        std::filesystem::path relocatable_sections_path;
        std::filesystem::path output_binary_path;
        std::vector<std::string> stubbed_funcs;
        std::vector<std::string> ignored_funcs;
        std::vector<InstructionPatch> instruction_patches;
        std::vector<FunctionHook> function_hooks;
        std::vector<FunctionSize> manual_func_sizes;
        std::vector<ManualFunction> manual_functions;
        std::string bss_section_suffix;
        std::string recomp_include;

        Config(const char* path);
        bool good() { return !bad; }
    private:
        bool bad;
    };
}

#endif
