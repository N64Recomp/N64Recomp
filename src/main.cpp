#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <span>

#include "rabbitizer.hpp"
#include "elfio/elfio.hpp"
#include "fmt/format.h"

#include "recomp_port.h"

std::unordered_set<std::string> ignored_funcs {
    // OS initialize functions
    "__createSpeedParam",
    "__osInitialize_common",
    "__osInitialize_autodetect",
    "osInitialize",
    // Audio interface functions
    "osAiGetLength",
    "osAiGetStatus",
    "osAiSetFrequency",
    "osAiSetNextBuffer",
    "__osAiDeviceBusy",
    // Video interface functions
    "osViBlack",
    "osViFade",
    "osViGetCurrentField",
    "osViGetCurrentFramebuffer",
    "osViGetCurrentLine",
    "osViGetCurrentMode",
    "osViGetNextFramebuffer",
    "osViGetStatus",
    "osViRepeatLine",
    "osViSetEvent",
    "osViSetMode",
    "osViSetSpecialFeatures",
    "osViSetXScale",
    "osViSetYScale",
    "osViSwapBuffer",
    "osCreateViManager",
    "viMgrMain",
    "__osViInit",
    "__osViSwapContext",
    "__osViGetCurrentContext",
    // RDP functions
    "osDpGetCounters",
    "osDpSetStatus",
    "osDpGetStatus",
    "osDpSetNextBuffer",
    "__osDpDeviceBusy",
    // RSP functions
    "osSpTaskLoad",
    "osSpTaskStartGo",
    "osSpTaskYield",
    "osSpTaskYielded",
    "__osSpDeviceBusy",
    "__osSpGetStatus",
    "__osSpRawStartDma",
    "__osSpRawReadIo",
    "__osSpRawWriteIo",
    "__osSpSetPc",
    "__osSpSetStatus",
    // Controller functions
    "osContGetQuery",
    "osContGetReadData",
    "osContInit",
    "osContReset",
    "osContSetCh",
    "osContStartQuery",
    "osContStartReadData",
    "__osContAddressCrc",
    "__osContDataCrc",
    "__osContGetInitData",
    "__osContRamRead",
    "__osContRamWrite",
    // EEPROM functions
    "osEepromLongRead",
    "osEepromLongWrite",
    "osEepromProbe",
    "osEepromRead",
    "osEepromWrite",
    "__osEepStatus",
    // Rumble functions
    "osMotorInit",
    "osMotorStart",
    "osMotorStop",
    // PFS functions
    "osPfsAllocateFile",
    "osPfsChecker",
    "osPfsDeleteFile",
    "osPfsFileState",
    "osPfsFindFile",
    "osPfsFreeBlocks",
    "osPfsGetLabel",
    "osPfsInit",
    "osPfsInitPak",
    "osPfsIsPlug",
    "osPfsNumFiles",
    "osPfsRepairId",
    "osPfsReadWriteFile",
    "__osPackEepReadData",
    "__osPackEepWriteData",
    "__osPackRamReadData",
    "__osPackRamWriteData",
    "__osPackReadData",
    "__osPackRequestData",
    "__osPfsGetInitData",
    "__osPfsGetOneChannelData",
    "__osPfsGetStatus",
    "__osPfsRequestData",
    "__osPfsRequestOneChannel",
    "__osPfsCreateAccessQueue",
    // Low level serial interface functions
    "__osSiDeviceBusy",
    "__osSiGetStatus",
    "__osSiRawStartDma",
    "__osSiRawReadIo",
    "__osSiRawWriteIo",
    "__osSiCreateAccessQueue",
    "__osSiGetAccess",
    "__osSiRelAccess",
    // Parallel interface (cartridge, DMA, etc.) functions
    "osCartRomInit",
    "osLeoDiskInit",
    "osCreatePiManager",
    "__osDevMgrMain",
    "osPiGetCmdQueue",
    "osPiGetStatus",
    "osPiReadIo",
    "osPiStartDma",
    "osPiWriteIo",
    "osEPiGetDeviceType",
    "osEPiStartDma",
    "osEPiWriteIo",
    "osEPiReadIo",
    "osPiRawStartDma",
    "osPiRawReadIo",
    "osPiRawWriteIo",
    "osEPiRawStartDma",
    "osEPiRawReadIo",
    "osEPiRawWriteIo",
    "__osPiRawStartDma",
    "__osPiRawReadIo",
    "__osPiRawWriteIo",
    "__osEPiRawStartDma",
    "__osEPiRawReadIo",
    "__osEPiRawWriteIo",
    "__osPiDeviceBusy",
    "__osPiCreateAccessQueue",
    "__osPiGetAccess",
    "__osPiRelAccess",
    "__osLeoAbnormalResume",
    "__osLeoInterrupt",
    "__osLeoResume",
    // Threading functions
    "osCreateThread",
    "osStartThread",
    "osStopThread",
    "osDestroyThread",
    "osYieldThread",
    "osSetThreadPri",
    "osGetThreadPri",
    "osGetThreadId",
    "__osDequeueThread",
    // Message Queue functions
    "osCreateMesgQueue",
    "osSendMesg",
    "osJamMesg",
    "osRecvMesg",
    "osSetEventMesg",
    // Timer functions
    "osStartTimer",
    "osSetTimer",
    "osStopTimer",
    "__osInsertTimer",
    "__osTimerInterrupt",
    "__osTimerServicesInit",
    "__osSetTimerIntr",
    // exceptasm functions
    "__osExceptionPreamble",
    "__osException",
    "send_mesg",
    "handle_CpU",
    "__osEnqueueAndYield",
    "__osEnqueueThread",
    "__osPopThread",
    "__osNop",
    "__osDispatchThread",
    "__osCleanupThread",
    "osGetCurrFaultedThread",
    "osGetNextFaultedThread",
    // interrupt functions
    "osSetIntMask",
    "osGetIntMask",
    "__osDisableInt",
    "__osRestoreInt",
    "__osSetGlobalIntMask",
    "__osResetGlobalIntMask",
    // TLB functions
    "osMapTLB",
    "osUnmapTLB",
    "osUnmapTLBAll",
    "osSetTLBASID",
    "osMapTLBRdb",
    "osVirtualToPhysical",
    "__osGetTLBHi",
    "__osGetTLBLo0",
    "__osGetTLBLo1",
    "__osGetTLBPageMask",
    "__osGetTLBASID",
    "__osProbeTLB",
    // Coprocessor 0 functions
    "__osSetCount",
    "osGetCount",
    "__osSetSR",
    "__osGetSR",
    "__osSetCause",
    "__osGetCause",
    "__osSetCompare",
    "__osGetCompare",
    "__osSetConfig",
    "__osGetConfig",
    "__osSetWatchLo",
    "__osGetWatchLo",
};

int main(int argc, char** argv) {
    if (argc != 2) {
        fmt::print("Usage: {} [input elf file]\n", argv[0]);
        std::exit(EXIT_SUCCESS);
    }

    ELFIO::elfio elf_file;

    auto exit_failure = [] (const std::string& error_str) {
        fmt::print(stderr, error_str);
        std::exit(EXIT_FAILURE);
    };

    if (!elf_file.load(argv[1])) {
        exit_failure("Failed to load provided elf file\n");
    }

    if (elf_file.get_class() != ELFIO::ELFCLASS32) {
        exit_failure("Incorrect elf class\n");
    }

    if (elf_file.get_encoding() != ELFIO::ELFDATA2MSB) {
        exit_failure("Incorrect endianness\n");
    }

    // Pointer to the symbol table section
    ELFIO::section* symtab_section = nullptr;
    // Size of the ROM as determined by the elf
    ELFIO::Elf_Xword rom_size = 0;
    // ROM address of each section
    std::vector<ELFIO::Elf_Xword> section_rom_addrs{};
    section_rom_addrs.resize(elf_file.sections.size());

    // Iterate over every section to record rom addresses and find the symbol table
    fmt::print("Sections\n");
    for (const std::unique_ptr<ELFIO::section>& section : elf_file.sections) {
        fmt::print("  {}: {} @ 0x{:08X}, 0x{:08X}\n", section->get_index(), section->get_name(), section->get_address(), rom_size);
        // Set the rom address of this section to the current accumulated ROM size
        section_rom_addrs[section->get_index()] = rom_size;
        // If this section isn't bss (SHT_NOBITS) and ends up in the rom (SHF_ALLOC), increase the rom size by this section's size
        if (section->get_type() != ELFIO::SHT_NOBITS && section->get_flags() & ELFIO::SHF_ALLOC) {
            rom_size += section->get_size();
        }
        // Check if this section is the symbol table and record it if so
        if (section->get_type() == ELFIO::SHT_SYMTAB) {
            symtab_section = section.get();
        }
    }

    // If no symbol table was found then exit
    if (symtab_section == nullptr) {
        exit_failure("No symbol section found\n");
    }

    ELFIO::symbol_section_accessor symbols{ elf_file, symtab_section };

    fmt::print("Num symbols: {}\n", symbols.get_symbols_num());

    std::vector<RecompPort::Function> functions{};
    functions.reserve(1024);

    for (int sym_index = 0; sym_index < symbols.get_symbols_num(); sym_index++) {
        std::string   name;
        ELFIO::Elf64_Addr    value;
        ELFIO::Elf_Xword     size;
        unsigned char bind;
        unsigned char type;
        ELFIO::Elf_Half      section_index;
        unsigned char other;

        // Read symbol properties
        symbols.get_symbol(sym_index, name, value, size, bind, type,
            section_index, other);

        // Check if this symbol is a function
        if (type == ELFIO::STT_FUNC) {
            auto section_rom_addr = section_rom_addrs[section_index];
            auto section_offset = value - elf_file.sections[section_index]->get_address();
            const uint32_t* words = reinterpret_cast<const uint32_t*>(elf_file.sections[section_index]->get_data() + section_offset);
            functions.emplace_back(
                static_cast<uint32_t>(value),
                static_cast<uint32_t>(section_offset + section_rom_addr),
                std::span{ reinterpret_cast<const uint32_t*>(words), size / 4 },
                std::move(name)
            );
        }
    }

    fmt::print("Function count: {}\n", functions.size());

    //#pragma omp parallel for
    for (size_t i = 0; i < functions.size(); i++) {
        const auto& func = functions[i];
        if (!ignored_funcs.contains(func.name)) {
            if (RecompPort::recompile_function(func, "out/" + func.name + ".c") == false) {
                fmt::print(stderr, "Error recompiling {}\n", func.name);
                std::exit(EXIT_FAILURE);
            }
        }
    }
    //RecompPort::recompile_function(functions.back(), "test.c");

    return 0;
}
