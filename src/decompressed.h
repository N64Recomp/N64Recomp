#ifndef N64RECOMP_DECOMPRESSED_H
#define N64RECOMP_DECOMPRESSED_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "recompiler/context.h"
#include "config.h"  // DecompressedSection

namespace N64Recomp {

// Adds one synthesized section per DecompressedSection to
// `context`. Reads the wrapper bytes directly from the on-disk ROM
// (NOT from `context.rom`, which only contains ELF-loaded bytes).
//
// On success, each new section appears at the end of context.sections
// with:
//   - section.ram_addr = config.vram
//   - section.rom_addr = synthetic, marks the original wrapper offset
//                        for traceability
//   - section.size     = relocOffset (the body+bss size from the
//                        FRAGMENT header)
//   - section.relocs   = parsed Stadium-format reloc table converted
//                        to N64Recomp::Reloc entries
//   - section.executable = true (FRAGMENT sections always contain code)
//   - section.relocatable = config.relocatable
// Plus one Function entry for the entry trampoline at vram + 0
// (typically a J insn jumping to vram + 0x20).
//
// Decompressed bytes are appended to `context.rom` so that the rest
// of the pipeline (which reads sections via `rom_addr`) finds them.
//
// Returns false (and prints to stderr) on the first config entry that
// fails to decompress or parse cleanly.
bool synthesize_decompressed_sections(
    Context& context,
    const std::filesystem::path& rom_path,
    const std::vector<DecompressedSection>& configs);

} // namespace N64Recomp

#endif
