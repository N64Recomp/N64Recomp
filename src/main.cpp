#include "rabbitizer.hpp"
#include "elfio/elfio.hpp"
#include "fmt/format.h"

#include <cstdio>

int main(int argc, char** argv) {
    uint32_t word = 0x8D4A7E18; // lw
    uint32_t vram = 0x80000000;
    int extraLJust = 5;
    rabbitizer::InstructionCpu instr(word, vram);

    fmt::print("{}\n", instr.isBranch());
    fmt::print("{:08X}: {}\n", word, instr.disassemble(extraLJust));

    return 0;
}
