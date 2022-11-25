#include <cstdio>
#include <fstream>
#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osSpTaskLoad_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

bool dump_frame = false;

extern "C" void osSpTaskStartGo_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    //printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    if (task->t.type == M_GFXTASK) {
        //printf("[sp] Gfx task: %08X\n", (uint32_t)ctx->r4);
    } else if (task->t.type == M_AUDTASK) {
        printf("[sp] Audio task: %08X\n", (uint32_t)ctx->r4);
    }
    // For debugging
    if (dump_frame) {
        char addr_str[32];
        constexpr size_t ram_size = 0x800000;
        std::unique_ptr<char[]> ram_unswapped = std::make_unique<char[]>(ram_size);
        sprintf(addr_str, "%08X", task->t.data_ptr);
        std::ofstream dump_file{ "../../ramdump" + std::string{ addr_str } + ".bin", std::ios::binary};

        for (size_t i = 0; i < ram_size; i++) {
            ram_unswapped[i] = rdram[i ^ 3];
        }

        dump_file.write(ram_unswapped.get(), ram_size);
        dump_frame = false;
    }
    Multilibultra::submit_rsp_task(rdram, ctx->r4);
}

extern "C" void osSpTaskYield_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osSpTaskYielded_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void __osSpSetPc_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
