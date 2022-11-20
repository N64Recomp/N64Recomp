#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cmath>
#include <unordered_map>
#include <fstream>
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

extern std::pair<uint32_t, recomp_func_t*> funcs[];
extern const size_t num_funcs;

std::unordered_map<uint32_t, recomp_func_t*> func_map{};

extern "C" recomp_func_t* get_function(uint32_t addr) {
    auto func_find = func_map.find(addr);
    if (func_find == func_map.end()) {
        fprintf(stderr, "Failed to find function at 0x%08X\n", addr);
        std::exit(EXIT_FAILURE);
    }
    return func_find->second;
}

extern "C" void bzero(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    gpr start_addr = ctx->r4;
    gpr size = ctx->r5;

    for (uint32_t i = 0; i < size; i++) {
        MEM_B(start_addr, i) = 0;
    }
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

extern "C" void init(uint8_t * restrict rdram, recomp_context * restrict ctx);

// rocket robot
//extern "C" void game_init(uint8_t* restrict rdram, recomp_context* restrict ctx);
// test rom
void do_rom_read(uint8_t* rdram, gpr ram_address, uint32_t dev_address, size_t num_bytes);

std::unique_ptr<uint8_t[]> rom;
size_t rom_size;

uint64_t start_time;

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s [baserom]\n", argv[0]);
        exit(EXIT_SUCCESS);
    }

    {
        std::basic_ifstream<uint8_t> rom_file{ argv[1], std::ios::binary };

        size_t iobuf_size = 0x100000;
        std::unique_ptr<uint8_t[]> iobuf = std::make_unique<uint8_t[]>(iobuf_size);
        rom_file.rdbuf()->pubsetbuf(iobuf.get(), iobuf_size);

        if (!rom_file) {
            fprintf(stderr, "Failed to open rom: %s\n", argv[1]);
            exit(EXIT_FAILURE);
        }

        rom_file.seekg(0, std::ios::end);
        rom_size = rom_file.tellg();
        rom_file.seekg(0, std::ios::beg);

        rom = std::make_unique<uint8_t[]>(rom_size);

        rom_file.read(rom.get(), rom_size);
    }

    // Byteswap the rom
    //for (size_t rom_addr = 0; rom_addr < rom_size; rom_addr += 4) {
    //    uint32_t word = *reinterpret_cast<uint32_t*>(rom.get() + rom_addr);
    //    word = byteswap(word);
    //    *reinterpret_cast<uint32_t*>(rom.get() + rom_addr) = word;
    //}

    // Get entrypoint from ROM
    // TODO fix this for other IPL3 versions
    gpr entrypoint = (int32_t)byteswap(*reinterpret_cast<uint32_t*>(rom.get() + 0x8));

    // Allocate rdram_buffer
    std::unique_ptr<uint8_t[]> rdram_buffer = std::make_unique<uint8_t[]>(8 * 1024 * 1024);
    std::memset(rdram_buffer.get(), 0, 8 * 1024 * 1024);
    recomp_context context{};

    // Initial 1MB DMA
    do_rom_read(rdram_buffer.get(), entrypoint, 0x1000, 0x100000);
    //std::copy_n(rom.get() + 0x1000, 0x100000, rdram_buffer.get() + entrypoint - 0x80000000);

    // Initialize function address map
    for (size_t i = 0; i < num_funcs; i++) {
        func_map[funcs[i].first] = funcs[i].second;
    }

    // TODO move this to a more appropriate place
#ifdef _WIN32
    {
        SYSTEMTIME st;
        FILETIME ft;
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &ft);

        start_time = ((uint64_t)ft.dwHighDateTime << 32) + ft.dwLowDateTime;
    }
#endif

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

    // Clear bss
    // TODO run the entrypoint instead
    // rocket robot
    //memset(rdram_buffer.get() + 0xAF860, 0, 0xC00A0u - 0XAF860);
    // test rom
    memset(rdram_buffer.get() + 0x18670, 0, 0x20D120);

    debug_printf("[Recomp] Starting\n");

    Multilibultra::preinit(rdram_buffer.get(), rom.get());

    // rocket robot
    // game_init(rdram_buffer.get(), &context);
    // test rom
    init(rdram_buffer.get(), &context);

    debug_printf("[Recomp] Quitting\n");

    return EXIT_SUCCESS;
}
