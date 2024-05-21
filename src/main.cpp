#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <span>
#include <filesystem>
#include <optional>

#include "rabbitizer.hpp"
#include "elfio/elfio.hpp"
#include "fmt/format.h"
#include "fmt/ostream.h"

#include "recomp_port.h"
#include <set>

std::unordered_set<std::string> reimplemented_funcs{
    // OS initialize functions
    "__osInitialize_common",
    "osInitialize",
    "osGetMemSize",
    // Audio interface functions
    "osAiGetLength",
    "osAiGetStatus",
    "osAiSetFrequency",
    "osAiSetNextBuffer",
    // Video interface functions
    "osViSetXScale",
    "osViSetYScale",
    "osCreateViManager",
    "osViBlack",
    "osViSetSpecialFeatures",
    "osViGetCurrentFramebuffer",
    "osViGetNextFramebuffer",
    "osViSwapBuffer",
    "osViSetMode",
    "osViSetEvent",
    // RDP functions
    "osDpSetNextBuffer",
    // RSP functions
    "osSpTaskLoad",
    "osSpTaskStartGo",
    "osSpTaskYield",
    "osSpTaskYielded",
    "__osSpSetPc",
    // Controller functions
    "osContInit",
    "osContStartReadData",
    "osContGetReadData",
    "osContStartQuery",
    "osContGetQuery",
    "osContSetCh",
    // EEPROM functions
    "osEepromProbe",
    "osEepromWrite",
    "osEepromLongWrite",
    "osEepromRead",
    "osEepromLongRead",
    // Rumble functions
    "__osMotorAccess",
    "osMotorInit",
    "osMotorStart",
    "osMotorStop",
    // PFS functions
    "osPfsInitPak",
    "osPfsFreeBlocks",
    "osPfsAllocateFile",
    "osPfsDeleteFile",
    "osPfsFileState",
    "osPfsFindFile",
    "osPfsReadWriteFile",
    // Parallel interface (cartridge, DMA, etc.) functions
    "osCartRomInit",
    "osCreatePiManager",
    "osPiStartDma",
    "osEPiStartDma",
    "osPiGetStatus",
    "osEPiRawStartDma",
    "osEPiReadIo",
    // Flash saving functions
    "osFlashInit",
    "osFlashReadStatus",
    "osFlashReadId",
    "osFlashClearStatus",
    "osFlashAllErase",
    "osFlashAllEraseThrough",
    "osFlashSectorErase",
    "osFlashSectorEraseThrough",
    "osFlashCheckEraseEnd",
    "osFlashWriteBuffer",
    "osFlashWriteArray",
    "osFlashReadArray",
    "osFlashChange",
    // Threading functions
    "osCreateThread",
    "osStartThread",
    "osStopThread",
    "osDestroyThread",
    "osSetThreadPri",
    "osGetThreadPri",
    "osGetThreadId",
    // Message Queue functions
    "osCreateMesgQueue",
    "osRecvMesg",
    "osSendMesg",
    "osJamMesg",
    "osSetEventMesg",
    // Timer functions
    "osGetTime",
    "osSetTimer",
    "osStopTimer",
    // Voice functions
    "osVoiceSetWord",
    "osVoiceCheckWord",
    "osVoiceStopReadData",
    "osVoiceInit",
    "osVoiceMaskDictionary",
    "osVoiceStartReadData",
    "osVoiceControlGain",
    "osVoiceGetReadData",
    "osVoiceClearDictionary",
    // interrupt functions
    "osSetIntMask",
    "__osDisableInt",
    "__osRestoreInt",
    // TLB functions
    "osVirtualToPhysical",
    // Coprocessor 0/1 functions
    "osGetCount",
    "__osSetFpcCsr",
    // Cache funcs
    "osInvalDCache",
    "osInvalICache",
    "osWritebackDCache",
    "osWritebackDCacheAll",
    // Debug functions
    "is_proutSyncPrintf",
    "__checkHardware_msp",
    "__checkHardware_kmc",
    "__checkHardware_isv",
    "__osInitialize_msp",
    "__osInitialize_kmc",
    "__osInitialize_isv",
    "__osRdbSend",
    // ido math routines
    "__ull_div",
    "__ll_div",
    "__ll_mul",
    "__ull_rem",
    "__ull_to_d",
    "__ull_to_f",
};

std::unordered_set<std::string> ignored_funcs {
    // OS initialize functions
    "__createSpeedParam",
    "__osInitialize_common",
    "osInitialize",
    "osGetMemSize",
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
    "__osContChannelReset",
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
    "__osMotorAccess",
    "_MakeMotorData",
    // Pack functions
    "__osCheckId",
    "__osCheckPackId",
    "__osGetId",
    "__osPfsRWInode",
    "__osRepairPackId",
    "__osPfsSelectBank",
    "__osCheckPackId",
    "ramromMain",
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
    "__osPfsCheckRamArea",
    "__osPfsGetNextPage",
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
    // Flash saving functions
    "osFlashInit",
    "osFlashReadStatus",
    "osFlashReadId",
    "osFlashClearStatus",
    "osFlashAllErase",
    "osFlashAllEraseThrough",
    "osFlashSectorErase",
    "osFlashSectorEraseThrough",
    "osFlashCheckEraseEnd",
    "osFlashWriteBuffer",
    "osFlashWriteArray",
    "osFlashReadArray",
    "osFlashChange",
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
    "osGetTime",
    "__osInsertTimer",
    "__osTimerInterrupt",
    "__osTimerServicesInit",
    "__osSetTimerIntr",
    // Voice functions
    "osVoiceSetWord",
    "osVoiceCheckWord",
    "osVoiceStopReadData",
    "osVoiceInit",
    "osVoiceMaskDictionary",
    "osVoiceStartReadData",
    "osVoiceControlGain",
    "osVoiceGetReadData",
    "osVoiceClearDictionary",
    "__osVoiceCheckResult",
    "__osVoiceContRead36",
    "__osVoiceContWrite20",
    "__osVoiceContWrite4",
    "__osVoiceContRead2",
    "__osVoiceSetADConverter",
    "__osVoiceContDataCrc",
    "__osVoiceGetStatus",
    "corrupted",
    "corrupted_init",
    // exceptasm functions
    "__osExceptionPreamble",
    "__osException",
    "__ptExceptionPreamble",
    "__ptException",
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
    // Coprocessor 0/1 functions
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
    "__osSetFpcCsr",
    // Cache funcs
    "osInvalDCache",
    "osInvalICache",
    "osWritebackDCache",
    "osWritebackDCacheAll",
    // Microcodes
    "rspbootTextStart",
    "gspF3DEX2_fifoTextStart",
    "gspS2DEX2_fifoTextStart",
    "gspL3DEX2_fifoTextStart",
    // Debug functions
    "msp_proutSyncPrintf",
    "__osInitialize_msp",
    "__checkHardware_msp",
    "kmc_proutSyncPrintf",
    "__osInitialize_kmc",
    "__checkHardware_kmc",
    "isPrintfInit",
    "is_proutSyncPrintf",
    "__osInitialize_isv",
    "__checkHardware_isv",
    "__isExpJP",
    "__isExp",
    "__osRdbSend",
    "__rmonSendData",
    "__rmonWriteMem",
    "__rmonReadWordAt",
    "__rmonWriteWordTo",
    "__rmonWriteMem",
    "__rmonSetSRegs",
    "__rmonSetVRegs",
    "__rmonStopThread",
    "__rmonGetThreadStatus",
    "__rmonGetVRegs",
    "__rmonHitSpBreak",
    "__rmonRunThread",
    "__rmonClearBreak",
    "__rmonGetBranchTarget",
    "__rmonGetSRegs",
    "__rmonSetBreak",
    "__rmonReadMem",
    "__rmonRunThread",
    "__rmonCopyWords",
    "__rmonExecute",
    "__rmonGetExceptionStatus",
    "__rmonGetExeName",
    "__rmonGetFRegisters",
    "__rmonGetGRegisters",
    "__rmonGetRegionCount",
    "__rmonGetRegions",
    "__rmonGetRegisterContents",
    "__rmonGetTCB",
    "__rmonHitBreak",
    "__rmonHitCpuFault",
    "__rmonIdleRCP",
    "__rmonInit",
    "__rmonIOflush",
    "__rmonIOhandler",
    "__rmonIOputw",
    "__rmonListBreak",
    "__rmonListProcesses",
    "__rmonListThreads",
    "__rmonLoadProgram",
    "__rmonMaskIdleThreadInts",
    "__rmonMemcpy",
    "__rmonPanic",
    "__rmonRCPrunning",
    "__rmonRunRCP",
    "__rmonSendFault",
    "__rmonSendHeader",
    "__rmonSendReply",
    "__rmonSetComm",
    "__rmonSetFault",
    "__rmonSetFRegisters",
    "__rmonSetGRegisters",
    "__rmonSetSingleStep",
    "__rmonStepRCP",
    "__rmonStopUserThreads",
    "__rmonThreadStatus",
    "__rmon",
    "__rmonRunThread",
    "rmonFindFaultedThreads",
    "rmonMain",
    "rmonPrintf",
    "rmonGetRcpRegister",
    "kdebugserver",
    "send",

    // ido math routines
    "__ll_div",
    "__ll_lshift",
    "__ll_mod",
    "__ll_mul",
    "__ll_rem",
    "__ll_rshift",
    "__ull_div",
    "__ull_divremi",
    "__ull_rem",
    "__ull_rshift",
    "__d_to_ll",
    "__f_to_ll",
    "__d_to_ull",
    "__f_to_ull",
    "__ll_to_d",
    "__ll_to_f",
    "__ull_to_d",
    "__ull_to_f",
    // Setjmp/longjmp for mario party
    "setjmp",
    "longjmp"
    // 64-bit functions for banjo
    "func_8025C29C",
    "func_8025C240",
    "func_8025C288",

    // rmonregs
    "LoadStoreSU",
    "LoadStoreVU",
    "SetUpForRCPop",
    "CleanupFromRCPop",
    "__rmonGetGRegisters",
    "__rmonSetGRegisters",
    "__rmonGetFRegisters",
    "__rmonSetFRegisters",
    "rmonGetRcpRegister",
    "__rmonGetSRegs",
    "__rmonSetSRegs",
    "__rmonGetVRegs",
    "__rmonSetVRegs",
    "__rmonGetRegisterContents",

    // rmonbrk
    "SetTempBreakpoint",
    "ClearTempBreakpoint",
    "__rmonSetBreak",
    "__rmonListBreak",
    "__rmonClearBreak",
    "__rmonGetBranchTarget",
    "IsJump",
    "__rmonSetSingleStep",
    "__rmonGetExceptionStatus",
    "rmonSendBreakMessage",
    "__rmonHitBreak",
    "__rmonHitSpBreak",
    "__rmonHitCpuFault",
    "rmonFindFaultedThreads",

    // kdebugserver
    "string_to_u32",
    "send_packet",
    "clear_IP6",
    "send",
    "kdebugserver",
};

std::unordered_set<std::string> renamed_funcs{
    // Math
    "sincosf",
    "sinf",
    "cosf",
    "__sinf",
    "__cosf",
    "asinf",
    "acosf",
    "atanf",
    "atan2f",
    "tanf",
    "sqrt",
    "sqrtf",

    // Memory
    "memcpy",
    "memset",
    "memmove",
    "memcmp",
    "strcmp",
    "strcat",
    "strcpy",
    "strchr",
    "strlen",
    "strtok",
    "sprintf",
    "bzero",
    "bcopy",
    "bcmp",

    // long jumps
    "setjmp",
    "longjmp",

    // Math 2
    "ldiv",
    "lldiv",
    "ceil",
    "ceilf",
    "floor",
    "floorf",
    "fmodf",
    "fmod",
    "modf",
    "lround",
    "lroundf",
    "nearbyint",
    "nearbyintf",
    "round",
    "roundf",
    "trunc",
    "truncf",

    // printf family
    "vsprintf",
    "gcvt",
    "fcvt",
    "ecvt",

    "__assert",

    // allocations
    "malloc",
    "free",
    "realloc",
    "calloc",

    // rand
    "rand",
    "srand",
    "random",

    // gzip
    "huft_build",
    "huft_free",
    "inflate_codes",
    "inflate_stored",
    "inflate_fixed",
    "inflate_dynamic",
    "inflate_block",
    "inflate",
    "expand_gzip",
    "auRomDataRead"
    "data_write",
    "unzip",
    "updcrc",
    "clear_bufs",
    "fill_inbuf",
    "flush_window",

    // libgcc math routines
    "__muldi3",
    "__divdi3",
    "__udivdi3",
    "__umoddi3",
    "div64_64",
    "div64_32",
    "__moddi3",
    "_matherr",
};

bool read_symbols(RecompPort::Context& context, const ELFIO::elfio& elf_file, ELFIO::section* symtab_section, uint32_t entrypoint, bool has_entrypoint, bool use_absolute_symbols) {
    bool found_entrypoint_func = false;
    ELFIO::symbol_section_accessor symbols{ elf_file, symtab_section };
    fmt::print("Num symbols: {}\n", symbols.get_symbols_num());

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

        // Read symbol properties
        symbols.get_symbol(sym_index, name, value, size, bind, type,
            section_index, other);

        if (section_index == ELFIO::SHN_ABS && use_absolute_symbols) {
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

        if (section_index >= context.sections.size()) {
            continue;
        }
        
        // Check if this symbol is the entrypoint
        if (has_entrypoint && value == entrypoint && type == ELFIO::STT_FUNC) {
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
        auto size_find = context.manually_sized_funcs.find(name);
        if (size_find != context.manually_sized_funcs.end()) {
            size = size_find->second;
            type = ELFIO::STT_FUNC;
        }

        if (reimplemented_funcs.contains(name)) {
            reimplemented = true;
            name = name + "_recomp";
            ignored = true;
        } else if (ignored_funcs.contains(name)) {
            name = name + "_recomp";
            ignored = true;
        }

        auto& section = context.sections[section_index];

        // Check if this symbol is a function or has no type (like a regular glabel would)
        // Symbols with no type have a dummy entry created so that their symbol can be looked up for function calls
        if (ignored || type == ELFIO::STT_FUNC || type == ELFIO::STT_NOTYPE || type == ELFIO::STT_OBJECT) {
            if (renamed_funcs.contains(name)) {
                name = name + "_recomp";
                ignored = false;
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
                if (has_entrypoint && rom_address == 0x1000 && type == ELFIO::STT_FUNC) {
                    vram = entrypoint;
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
                }
                context.functions_by_name[name] = context.functions.size();

                std::vector<uint32_t> insn_words(num_instructions);
                insn_words.assign(words, words + num_instructions);

                context.functions.emplace_back(
                    vram,
                    rom_address,
                    std::move(insn_words),
                    std::move(name),
                    section_index,
                    ignored,
                    reimplemented
                );
            } else {
                uint32_t vram = static_cast<uint32_t>(value);
                section.function_addrs.push_back(vram);
                context.functions_by_vram[vram].push_back(context.functions.size());
                context.functions.emplace_back(
                    vram,
                    0,
                    std::vector<uint32_t>{},
                    std::move(name),
                    section_index,
                    ignored,
                    reimplemented
                );
            }
        }
    }

    return found_entrypoint_func;
}

void add_manual_functions(RecompPort::Context& context, const ELFIO::elfio& elf_file, const std::vector<RecompPort::ManualFunction>& manual_funcs) {
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

    for (const RecompPort::ManualFunction& cur_func_def : manual_funcs) {
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
        const uint32_t* elf_words = reinterpret_cast<const uint32_t*>(elf_file.sections[section_index]->get_data() + section_offset);

        words.assign(elf_words, elf_words + words.size());

        size_t function_index = context.functions.size();
        context.functions.emplace_back(
            cur_func_def.vram,
            rom_address,
            std::move(words),
            cur_func_def.func_name,
            ELFIO::Elf_Half(section_index),
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

ELFIO::section* read_sections(RecompPort::Context& context, const RecompPort::Config& config, const ELFIO::elfio& elf_file) {
    ELFIO::section* symtab_section = nullptr;
    std::vector<SegmentEntry> segments{};
    segments.resize(elf_file.segments.size());

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
    fmt::print("Sections\n");
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

        if (context.relocatable_sections.contains(section_name)) {
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

            // If this reloc section is for a section that has been marked as relocatable, record it in the reloc section lookup
            if (context.relocatable_sections.contains(reloc_target_section)) {
                reloc_sections_by_name[reloc_target_section] = section.get();
            }
        }

        // If the section is bss (SHT_NOBITS) and ends with the bss suffix, add it to the bss section map
        if (type == ELFIO::SHT_NOBITS && section_name.ends_with(config.bss_section_suffix)) {
            std::string bss_target_section = section_name.substr(0, section_name.size() - config.bss_section_suffix.size());

            // If this bss section is for a section that has been marked as relocatable, record it in the reloc section lookup
            if (context.relocatable_sections.contains(bss_target_section)) {
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
            section_out.rom_addr = (ELFIO::Elf_Xword)-1;
        }
        // Check if this section is marked as executable, which means it has code in it
        if (section->get_flags() & ELFIO::SHF_EXECINSTR) {
            section_out.executable = true;
            context.executable_section_count++;
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
    for (RecompPort::Section &section_out : context.sections) {
        // Check if a bss section was found that corresponds with this section
        auto bss_find = bss_sections_by_name.find(section_out.name);
        if (bss_find != bss_sections_by_name.end()) {
            section_out.bss_section_index = bss_find->second->get_index();
        }

        if (section_out.relocatable) {
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

                    RecompPort::Reloc& reloc_out = section_out.relocs[i];

                    // Get the real full_immediate by extracting the immediate from the instruction
                    uint32_t instr_word = byteswap(*reinterpret_cast<const uint32_t*>(context.rom.data() + section_out.rom_addr + rel_offset - section_out.ram_addr));
                    rabbitizer::InstructionCpu instr{ instr_word, static_cast<uint32_t>(rel_offset) };
                    //context.rom section_out.rom_addr;

                    reloc_out.address = rel_offset;
                    reloc_out.symbol_index = rel_symbol;
                    reloc_out.type = static_cast<RecompPort::RelocType>(rel_type);

                    std::string       rel_symbol_name;
                    ELFIO::Elf64_Addr rel_symbol_value;
                    ELFIO::Elf_Xword  rel_symbol_size;
                    unsigned char     rel_symbol_bind;
                    unsigned char     rel_symbol_type;
                    ELFIO::Elf_Half   rel_symbol_section_index;
                    unsigned char     rel_symbol_other;

                    bool found_rel_symbol = symbol_accessor.get_symbol(
                        rel_symbol, rel_symbol_name, rel_symbol_value, rel_symbol_size, rel_symbol_bind, rel_symbol_type, rel_symbol_section_index, rel_symbol_other);

                    reloc_out.target_section = rel_symbol_section_index;

                    // Reloc pairing, see MIPS System V ABI documentation page 4-18 (https://refspecs.linuxfoundation.org/elf/mipsabi.pdf)
                    if (reloc_out.type == RecompPort::RelocType::R_MIPS_LO16) {
                        if (prev_hi) {
                            if (prev_hi_symbol != rel_symbol) {
                                fmt::print(stderr, "Paired HI16 and LO16 relocations have different symbols\n"
                                                    "  LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                    i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                return nullptr;
                            }
                            uint32_t rel_immediate = instr.getProcessedImmediate();
                            uint32_t full_immediate = (prev_hi_immediate << 16) + (int16_t)rel_immediate;


                            // Set this and the previous HI16 relocs' relocated addresses
                            section_out.relocs[i - 1].target_address = full_immediate;
                            reloc_out.target_address = full_immediate;
                        }
                        else {
                            if (prev_lo) {
                                uint32_t rel_immediate = instr.getProcessedImmediate();
                                uint32_t full_immediate;

                                if (prev_hi_symbol != rel_symbol) {
                                    fmt::print(stderr, "[WARN] LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X} follows LO16 with different symbol\n",
                                        i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                }

                                full_immediate = (prev_hi_immediate << 16) + (int16_t)rel_immediate;
                                reloc_out.target_address = full_immediate;
                            }
                            else {
                                fmt::print(stderr, "Unpaired LO16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                    i, section_out.name, reloc_out.symbol_index, reloc_out.address);
                                return nullptr;
                            }
                        }
                        prev_lo = true;
                    } else {
                        if (prev_hi) {
                            fmt::print(stderr, "Unpaired HI16 reloc index {} in section {} referencing symbol {} with offset 0x{:08X}\n",
                                i - 1, section_out.name, section_out.relocs[i - 1].symbol_index, section_out.relocs[i - 1].address);
                            return nullptr;
                        }
                        prev_lo = false;
                    }

                    if (reloc_out.type == RecompPort::RelocType::R_MIPS_HI16) {
                        uint32_t rel_immediate = instr.getProcessedImmediate();
                        prev_hi = true;
                        prev_hi_immediate = rel_immediate;
                        prev_hi_symbol = rel_symbol;
                    } else {
                        prev_hi = false;
                    }

                    if (reloc_out.type == RecompPort::RelocType::R_MIPS_32) {
                        // Nothing to do here
                    }
                }
            }

            // Sort this section's relocs by address, which allows for binary searching and more efficient iteration during recompilation.
            // This is safe to do as the entire full_immediate in present in relocs due to the pairing that was done earlier, so the HI16 does not
            // need to directly preceed the matching LO16 anymore.
            std::sort(section_out.relocs.begin(), section_out.relocs.end(), 
                [](const RecompPort::Reloc& a, const RecompPort::Reloc& b) {
                    return a.address < b.address;
                }
            );
        }
    }

    return symtab_section;
}

template<typename Iterator, typename Pred, typename Operation> void
for_each_if(Iterator begin, Iterator end, Pred p, Operation op) {
    for (; begin != end; begin++) {
        if (p(*begin)) {
            op(*begin);
        }
    }
}

void analyze_sections(RecompPort::Context& context, const ELFIO::elfio& elf_file) {
    std::vector<RecompPort::Section*> executable_sections{};
    
    executable_sections.reserve(context.executable_section_count);

    for_each_if(context.sections.begin(), context.sections.end(),
        [](const RecompPort::Section& section) {
            return section.executable && section.rom_addr >= 0x1000;
        },
        [&](RecompPort::Section& section) {
            executable_sections.push_back(&section);
        }
    );

    std::sort(executable_sections.begin(), executable_sections.end(),
        [](const RecompPort::Section* a, const RecompPort::Section* b) {
            return a->ram_addr < b->ram_addr;
        }
    );
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

bool recompile_single_function(const RecompPort::Context& context, const RecompPort::Config& config, const RecompPort::Function& func, const std::filesystem::path& output_path, std::span<std::vector<uint32_t>> static_funcs_out) {
    // Open the temporary output file
    std::filesystem::path temp_path = output_path;
    temp_path.replace_extension(".tmp");
    std::ofstream output_file{ temp_path };
    if (!output_file.good()) {
        fmt::print(stderr, "Failed to open file for writing: {}\n", temp_path.string() );
        return false;
    }

    if (!RecompPort::recompile_function(context, config, func, output_file, static_funcs_out, true)) {
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

void dump_context(const RecompPort::Context& context, const std::filesystem::path& path) {
    std::ofstream context_file {path};

    for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
        const RecompPort::Section& section = context.sections[section_index];
        const std::vector<size_t>& section_funcs = context.section_functions[section_index];
        if (!section_funcs.empty()) {
            fmt::print(context_file,
                "# Autogenerated from an ELF via N64Recomp\n"
                "[[section]]\n"
                "name = \"{}\"\n"
                "rom = 0x{:08X}\n"
                "vram = 0x{:08X}\n"
                "size = 0x{:X}\n"
                "\n",
                section.name, section.rom_addr, section.ram_addr, section.size);

            if (!section.relocs.empty()) {
                fmt::print(context_file, "relocs = [\n");

                for (const RecompPort::Reloc& reloc : section.relocs) {
                    if (reloc.target_section == section_index || reloc.target_section == section.bss_section_index) {
                        // TODO allow MIPS32 relocs for TLB mapping support.
                        if (reloc.type == RecompPort::RelocType::R_MIPS_HI16 || reloc.type == RecompPort::RelocType::R_MIPS_LO16) {
                            fmt::print(context_file, "    {{ type = \"{}\", vram = 0x{:08X}, target_vram = 0x{:08X} }},\n",
                                reloc_names[static_cast<int>(reloc.type)], reloc.address, reloc.target_address);
                        }
                    }
                }

                fmt::print(context_file, "]\n\n");
            }

            fmt::print(context_file, "functions = [\n");

            for (const size_t& function_index : section_funcs) {
                const RecompPort::Function& func = context.functions[function_index];
                fmt::print(context_file, "    {{ name = \"{}\", vram = 0x{:08X}, size = 0x{:X} }},\n",
                    func.name, func.vram, func.words.size() * sizeof(func.words[0]));
            }

            fmt::print(context_file, "]\n\n");
        }
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

    if (argc != 2) {
        fmt::print("Usage: {} [config file]\n", argv[0]);
        std::exit(EXIT_SUCCESS);
    }

    const char* config_path = argv[1];

    RecompPort::Config config{ config_path };
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

    RecompPort::Context context{};
    
    if (!config.elf_path.empty() && !config.symbols_file_path.empty()) {
        exit_failure("Config file cannot provide both an elf and a symbols file\n");
    }

    // Build a context from the provided elf file.
    if (!config.elf_path.empty()) {
        ELFIO::elfio elf_file;

        if (!elf_file.load(config.elf_path.string())) {
            exit_failure("Failed to load provided elf file\n");
        }

        if (elf_file.get_class() != ELFIO::ELFCLASS32) {
            exit_failure("Incorrect elf class\n");
        }

        if (elf_file.get_encoding() != ELFIO::ELFDATA2MSB) {
            exit_failure("Incorrect endianness\n");
        }

        context = { elf_file };
        context.relocatable_sections = std::move(relocatable_sections);

        // Read all of the sections in the elf and look for the symbol table section
        ELFIO::section* symtab_section = read_sections(context, config, elf_file);

        // Search the sections to see if any are overlays or TLB-mapped
        analyze_sections(context, elf_file);

        // If no symbol table was found then exit
        if (symtab_section == nullptr) {
            exit_failure("No symbol table section found\n");
        }

        // Manually sized functions
        for (const auto& func_size : config.manual_func_sizes) {
            context.manually_sized_funcs.emplace(func_size.func_name, func_size.size_bytes);
        }

        // Read all of the symbols in the elf and look for the entrypoint function
        bool found_entrypoint_func = read_symbols(context, elf_file, symtab_section, config.entrypoint, config.has_entrypoint, config.use_absolute_symbols);

        // Add any manual functions
        add_manual_functions(context, elf_file, config.manual_functions);

        if (config.has_entrypoint && !found_entrypoint_func) {
            exit_failure("Could not find entrypoint function\n");
        }
    }
    // Build a context from the provided symbols file.
    else if (!config.symbols_file_path.empty()) {
        if (config.rom_file_path.empty()) {
            exit_failure("A ROM file must be provided when using a symbols file\n");
        }

        std::vector<uint8_t> rom = read_file(config.rom_file_path);
        if (rom.empty()) {
            exit_failure("Failed to load ROM file: " + config.rom_file_path.string() + "\n");
        }
        
        if (!RecompPort::Context::from_symbol_file(config.symbols_file_path, std::move(rom), context)) {
            exit_failure("Failed to load symbols file\n");
        }

        auto rename_function = [&context](size_t func_index, const std::string& new_name) {
            RecompPort::Function& func = context.functions[func_index];

            context.functions_by_name.erase(func.name);
            func.name = new_name;
            context.functions_by_name[func.name] = func_index;
        };

        for (size_t func_index = 0; func_index < context.functions.size(); func_index++) {
            RecompPort::Function& func = context.functions[func_index];
            if (reimplemented_funcs.contains(func.name)) {
                rename_function(func_index, func.name + "_recomp");
                func.reimplemented = true;
                func.ignored = true;
            } else if (ignored_funcs.contains(func.name)) {
                rename_function(func_index, func.name + "_recomp");
                func.ignored = true;
            } else if (renamed_funcs.contains(func.name)) {
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
        "#include \"librecomp/recomp.h\"\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {{\n"
        "#endif\n"
        "\n"
    );

    std::vector<std::vector<uint32_t>> static_funcs_by_section{ context.sections.size() };

    // TODO expose a way to dump the context from the command line. Make sure not to rename functions when doing so.
    //fmt::print("Dumping context\n");
    //dump_context(context, "dump.toml");
    //return 0;

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
        // Mark the function as .
        context.functions[func_find->second].ignored = true;
    }

    // Apply any single-instruction patches.
    for (const RecompPort::InstructionPatch& patch : config.instruction_patches) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(patch.func_name);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to stub it out.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} has an instruction patch but does not exist!", patch.func_name));
        }

        RecompPort::Function& func = context.functions[func_find->second];
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
    for (const RecompPort::FunctionHook& patch : config.function_hooks) {
        // Check if the specified function exists.
        auto func_find = context.functions_by_name.find(patch.func_name);
        if (func_find == context.functions_by_name.end()) {
            // Function doesn't exist, present an error to the user instead of silently failing to stub it out.
            // This helps prevent typos in the config file or functions renamed between versions from causing issues.
            exit_failure(fmt::format("Function {} has a function hook but does not exist!", patch.func_name));
        }

        RecompPort::Function& func = context.functions[func_find->second];
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

    std::ofstream single_output_file;

    if (config.single_file_output) {
        single_output_file.open(config.output_func_path / config.elf_path.stem().replace_extension(".c"));
        // Write the file header
        fmt::print(single_output_file,
            "#include \"librecomp/recomp.h\"\n"
            "#include \"funcs.h\"\n"
            "\n");
    }

    //#pragma omp parallel for
    for (size_t i = 0; i < context.functions.size(); i++) {
        const auto& func = context.functions[i];

        if (!func.ignored && func.words.size() != 0) {
            fmt::print(func_header_file,
                "void {}(uint8_t* rdram, recomp_context* ctx);\n", func.name);
            bool result;
            if (config.single_file_output) {
                result = RecompPort::recompile_function(context, config, func, single_output_file, static_funcs_by_section, false);
            }
            else {
                result = recompile_single_function(context, config, func, config.output_func_path / (func.name + ".c"), static_funcs_by_section);
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

            RecompPort::Function func {
                static_func_addr,
                rom_addr,
                std::move(insn_words),
                fmt::format("static_{}_{:08X}", section_index, static_func_addr),
                static_cast<ELFIO::Elf_Half>(section_index),
                false
            };

            fmt::print(func_header_file,
                       "void {}(uint8_t* rdram, recomp_context* ctx);\n", func.name);

            bool result;
            size_t prev_num_statics = static_funcs_by_section[func.section_index].size();

            if (config.single_file_output) {
                result = RecompPort::recompile_function(context, config, func, single_output_file, static_funcs_by_section, false);
            }
            else {
                result = recompile_single_function(context, config, func, config.output_func_path / (func.name + ".c"), static_funcs_by_section);
            }

            // Add any new static functions that were found while recompiling this one.
            size_t cur_num_statics = static_funcs_by_section[func.section_index].size();
            if (cur_num_statics != prev_num_statics) {
                for (size_t new_static_index = prev_num_statics; new_static_index < cur_num_statics; new_static_index++) {
                    uint32_t new_static_vram = static_funcs_by_section[func.section_index][new_static_index];

                    if (!statics_set.contains(new_static_vram)) {
                        statics_set.emplace(new_static_vram);
                        section_statics.push_back(new_static_vram);
                    }
                }
            }

            if (result == false) {
                fmt::print(stderr, "Error recompiling {}\n", func.name);
                std::exit(EXIT_FAILURE);
            }
        }
    }

    if (config.has_entrypoint) {
        std::ofstream lookup_file{ config.output_func_path / "lookup.cpp" };
        
        fmt::print(lookup_file,
            "#include \"librecomp/recomp.h\"\n"
            "\n"
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

    fmt::print(func_header_file,
        "\n"
        "#ifdef __cplusplus\n"
        "}}\n"
        "#endif\n"
    );

    {
        std::ofstream overlay_file(config.output_func_path / "recomp_overlays.inl");
        std::string section_load_table = "static SectionTableEntry section_table[] = {\n";

        fmt::print(overlay_file, 
            "#include \"librecomp/recomp.h\"\n"
            "#include \"funcs.h\"\n"
            "#include \"librecomp/sections.h\"\n"
            "\n"
        );

        std::unordered_map<std::string, size_t> relocatable_section_indices{};
        size_t written_sections = 0;

        for (size_t section_index = 0; section_index < context.sections.size(); section_index++) {
            const auto& section = context.sections[section_index];
            const auto& section_funcs = context.section_functions[section_index];

            if (!section_funcs.empty()) {
                std::string_view section_name_trimmed{ section.name };

                if (section.relocatable) {
                    relocatable_section_indices.emplace(section.name, written_sections);
                }

                while (section_name_trimmed[0] == '.') {
                    section_name_trimmed.remove_prefix(1);
                }

                std::string section_funcs_array_name = fmt::format("section_{}_{}_funcs", section_index, section_name_trimmed);

                section_load_table += fmt::format("    {{ .rom_addr = 0x{0:08X}, .ram_addr = 0x{1:08X}, .size = 0x{2:08X}, .funcs = {3}, .num_funcs = ARRLEN({3}), .index = {4} }},\n",
                                                  section.rom_addr, section.ram_addr, section.size, section_funcs_array_name, section_index);

                fmt::print(overlay_file, "static FuncEntry {}[] = {{\n", section_funcs_array_name);

                for (size_t func_index : section_funcs) {
                    const auto& func = context.functions[func_index];

                    if (func.reimplemented || (!func.name.empty() && !func.ignored && func.words.size() != 0)) {
                        fmt::print(overlay_file, "    {{ .func = {}, .offset = 0x{:08x} }},\n", func.name, func.rom - section.rom_addr);
                    }
                }

                fmt::print(overlay_file, "}};\n");
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
    }

    return 0;
}
