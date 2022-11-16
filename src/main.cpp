#include "rabbitizer.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    uint32_t word = 0x8D4A7E18; // lw
    uint32_t vram = 0x80000000;
    int extraLJust = 5;
    rabbitizer::InstructionCpu instr(word, vram);

    printf("%08X: %s\n", word, instr.disassemble(extraLJust).c_str());

    return 0;
}
