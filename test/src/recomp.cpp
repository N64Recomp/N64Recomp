#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cmath>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include "recomp.h"
#include "../portultra/multilibultra.hpp"

#ifdef _MSC_VER
inline uint32_t byteswap(uint32_t val) {
    return _byteswap_ulong(val);
}
#else
constexpr uint32_t byteswap(uint32_t val) {
    return __builtin_bswap32(val);
}
#endif

extern "C" void _bzero(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    gpr start_addr = ctx->r4;
    gpr size = ctx->r5;

    for (uint32_t i = 0; i < size; i++) {
        MEM_B(start_addr, i) = 0;
    }
}

extern "C" void osGetMemSize_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
    ctx->r2 = 8 * 1024 * 1024;
}

extern "C" void switch_error(const char* func, uint32_t vram, uint32_t jtbl) {
    printf("Switch-case out of bounds in %s at 0x%08X for jump table at 0x%08X\n", func, vram, jtbl);
    exit(EXIT_FAILURE);
}

extern "C" void do_break(uint32_t vram) {
    printf("Encountered break at original vram 0x%08X\n", vram);
    exit(EXIT_FAILURE);
}

void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg) {
    recomp_context ctx{};
    ctx.r29 = sp;
    ctx.r4 = arg;
    recomp_func_t* func = get_function(addr);
    func(rdram, &ctx);
}

void do_rom_read(uint8_t* rdram, gpr ram_address, uint32_t dev_address, size_t num_bytes);

std::unique_ptr<uint8_t[]> rom;
size_t rom_size;

// Recomp generation functions
extern "C" void recomp_entrypoint(uint8_t * restrict rdram, recomp_context * restrict ctx);
gpr get_entrypoint_address();
const char* get_rom_name();
void init_overlays();
extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);

#ifdef _WIN32
#include <Windows.h>
#endif

int main(int argc, char **argv) {
    //if (argc != 2) {
    //    printf("Usage: %s [baserom]\n", argv[0]);
    //    exit(EXIT_SUCCESS);
    //}

#ifdef _WIN32
    // Set up console output to accept UTF-8 on windows
    SetConsoleOutputCP(CP_UTF8);

    // Change to a font that supports Japanese characters
    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof cfi;
    cfi.nFont = 0;
    cfi.dwFontSize.X = 0;
    cfi.dwFontSize.Y = 16;
    cfi.FontFamily = FF_DONTCARE;
    cfi.FontWeight = FW_NORMAL;
    wcscpy_s(cfi.FaceName, L"NSimSun");
    SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi);
#else
    std::setlocale(LC_ALL, "en_US.UTF-8");
#endif

    {
        std::basic_ifstream<uint8_t> rom_file{ get_rom_name(), std::ios::binary };

        size_t iobuf_size = 0x100000;
        std::unique_ptr<uint8_t[]> iobuf = std::make_unique<uint8_t[]>(iobuf_size);
        rom_file.rdbuf()->pubsetbuf(iobuf.get(), iobuf_size);

        if (!rom_file) {
            fprintf(stderr, "Failed to open rom: %s\n", get_rom_name());
            exit(EXIT_FAILURE);
        }

        rom_file.seekg(0, std::ios::end);
        rom_size = rom_file.tellg();
        rom_file.seekg(0, std::ios::beg);

        rom = std::make_unique<uint8_t[]>(rom_size);

        rom_file.read(rom.get(), rom_size);

        // TODO remove this
        // Modify the name in the rom header so RT64 doesn't find it
        rom[0x2F] = 'O';
    }

    // Initialize the overlays
    init_overlays();

    // Get entrypoint from recomp function
    gpr entrypoint = get_entrypoint_address();

    // Load overlays in the first 1MB
    load_overlays(0x1000, (int32_t)entrypoint, 1024 * 1024);

    // Allocate rdram_buffer (16MB to give room for any extra addressable data used by recomp)
    std::unique_ptr<uint8_t[]> rdram_buffer = std::make_unique<uint8_t[]>(16 * 1024 * 1024);
    std::memset(rdram_buffer.get(), 0, 8 * 1024 * 1024);
    recomp_context context{};

    // Initial 1MB DMA (rom address 0x1000 = physical address 0x10001000)
    do_rom_read(rdram_buffer.get(), entrypoint, 0x10001000, 0x100000);

    // Set up stack pointer
    context.r29 = 0xFFFFFFFF803FFFF0u;

    // Initialize variables normally set by IPL3
    constexpr int32_t osTvType = 0x80000300;
    constexpr int32_t osRomType = 0x80000304;
    constexpr int32_t osRomBase = 0x80000308;
    constexpr int32_t osResetType = 0x8000030c;
    constexpr int32_t osCicId = 0x80000310;
    constexpr int32_t osVersion = 0x80000314;
    constexpr int32_t osMemSize = 0x80000318;
    constexpr int32_t osAppNMIBuffer = 0x8000031c;
    uint8_t *rdram = rdram_buffer.get();
    MEM_W(osTvType, 0) = 1; // NTSC
    MEM_W(osRomBase, 0) = 0xB0000000u; // standard rom base
    MEM_W(osResetType, 0) = 0; // cold reset
    MEM_W(osMemSize, 0) = 8 * 1024 * 1024; // 8MB

    debug_printf("[Recomp] Starting\n");

    Multilibultra::preinit(rdram_buffer.get(), rom.get());

    recomp_entrypoint(rdram_buffer.get(), &context);

    debug_printf("[Recomp] Quitting\n");

    return EXIT_SUCCESS;
}
