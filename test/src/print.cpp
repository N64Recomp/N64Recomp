#include "../portultra/ultra64.h"
#include "../portultra/multilibultra.hpp"
#include "recomp.h"
#include "euc-jp.h"

extern "C" void __checkHardware_msp_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = 0;
}

extern "C" void __checkHardware_kmc_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = 0;
}

extern "C" void __checkHardware_isv_recomp(uint8_t * rdram, recomp_context * ctx) {
    ctx->r2 = 0;
}

extern "C" void __osInitialize_msp_recomp(uint8_t * rdram, recomp_context * ctx) {
}

extern "C" void __osInitialize_kmc_recomp(uint8_t * rdram, recomp_context * ctx) {
}

extern "C" void __osInitialize_isv_recomp(uint8_t * rdram, recomp_context * ctx) {
}

extern "C" void isPrintfInit_recomp(uint8_t * rdram, recomp_context * ctx) {
}

extern "C" void __osRdbSend_recomp(uint8_t * rdram, recomp_context * ctx) {
    gpr buf = ctx->r4;
    size_t size = ctx->r5;
    u32 type = (u32)ctx->r6;
    std::unique_ptr<char[]> to_print = std::make_unique<char[]>(size + 1);

    for (size_t i = 0; i < size; i++) {
        to_print[i] = MEM_B(i, buf);
    }
    to_print[size] = '\x00';

    fwrite(to_print.get(), 1, size, stdout);

    ctx->r2 = size;
}

extern "C" void is_proutSyncPrintf_recomp(uint8_t * rdram, recomp_context * ctx) {
    // Buffering to speed up print performance
    static std::vector<char> print_buffer;

    gpr buf = ctx->r5;
    size_t size = ctx->r6;

    for (size_t i = 0; i < size; i++) {
       // Add the new character to the buffer
       char cur_char = MEM_B(i, buf);

       // If the new character is a newline, flush the buffer
       if (cur_char == '\n') {
           std::string utf8_str = Encoding::decode_eucjp(std::string_view{ print_buffer.data(), print_buffer.size() });
           puts(utf8_str.c_str());
           print_buffer.clear();
       } else {
           print_buffer.push_back(cur_char);
       }
    }

    //fwrite(to_print.get(), size, 1, stdout);

    ctx->r2 = 1;
}
