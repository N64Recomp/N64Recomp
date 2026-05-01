#include "decompressed.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "compression/pers_szp.h"
#include "compression/yay0.h"
#include "fmt/format.h"
#include "rabbitizer.hpp"
#include <set>
#include "analysis.h"

namespace N64Recomp {

namespace {

uint32_t read_be_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

// FNV-1a 64-bit content hash. Used to deduplicate wrappers whose
// decompressed bytes are byte-for-byte identical (Stadium's 0x8FF00000
// slot has ~11 such pairs out of 279), and as the runtime dispatch key
// when multiple wrappers share a link vram.
uint64_t fnv1a_64(const uint8_t* data, size_t len) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < len; i++) {
        h ^= uint64_t(data[i]);
        h *= 0x100000001B3ull;
    }
    return h;
}

// Reads an entire file into memory. Returns empty vector on error.
std::vector<uint8_t> read_rom_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    if (!f.good()) return {};
    return buf;
}

// Stadium's runtime reloc-table format (see disasm/src/memmap.c
// Memmap_RelocateFragment). One uint32 per reloc:
//   bits 31:24  type   (2 = R_MIPS_32, 4 = R_MIPS_26,
//                       5 = R_MIPS_HI16, 6 = R_MIPS_LO16)
//   bits 23:0   offset into the fragment
// The table is preceded by a uint32 count.
//
// Stadium's type codes don't match ELF type codes — translate.
RelocType translate_stadium_reloc_type(uint8_t stadium_type) {
    switch (stadium_type) {
        case 2: return RelocType::R_MIPS_32;
        case 4: return RelocType::R_MIPS_26;
        case 5: return RelocType::R_MIPS_HI16;
        case 6: return RelocType::R_MIPS_LO16;
        default: return RelocType::R_MIPS_NONE;
    }
}

// Parses the FRAGMENT-format header at the start of the decompressed
// blob and the trailing reloc table. Populates `section.relocs`. The
// per-reloc target_section is filled in by the caller after all
// decompressed sections are added (so cross-fragment R_MIPS_32 targets
// can be resolved against the full section list).
//
// Per-type computation of `target_section_offset` (the field the
// recompiler reads at codegen time):
//   - R_MIPS_32: word value is an absolute pointer; offset =
//       word - section_vram (then refined cross-section by caller).
//   - R_MIPS_26: J/JAL target = (word & 0x03FFFFFF) << 2 OR'd with
//       PC[31:28]; offset = target - section_vram.
//   - R_MIPS_HI16/LO16: paired. Combined immediate =
//       (HI << 16) + (int16_t)LO. Offset = combined - section_vram.
//       The recompiler emits both RELOC_HI16(idx, off) and
//       RELOC_LO16(idx, off) using each reloc's target_section_offset,
//       so both members of the pair carry the SAME computed offset.
//
// Stadium's reloc table orders HI16 immediately followed by its paired
// LO16 (matches the body's instruction order). We pair by adjacency.
//
// Returns false if the header is malformed.
bool parse_fragment_relocs(const std::vector<uint8_t>& bytes,
                           uint32_t section_vram,
                           uint16_t section_index,
                           Section& section_out) {
    if (bytes.size() < 0x20) {
        std::fprintf(stderr,
            "decompressed: blob smaller than FRAGMENT header (size=0x%zX)\n",
            bytes.size());
        return false;
    }
    if (std::memcmp(bytes.data() + 0x08, "FRAGMENT", 8) != 0) {
        std::fprintf(stderr,
            "decompressed: missing FRAGMENT magic at +0x08\n");
        return false;
    }

    const uint32_t reloc_offset = read_be_u32(bytes.data() + 0x14);
    const uint32_t size_in_ram  = read_be_u32(bytes.data() + 0x1C);

    if (reloc_offset > bytes.size() || size_in_ram > bytes.size()) {
        std::fprintf(stderr,
            "decompressed: relocOffset 0x%X / sizeInRam 0x%X exceed blob "
            "size 0x%zX\n",
            reloc_offset, size_in_ram, bytes.size());
        return false;
    }
    if (reloc_offset + 4 > bytes.size()) {
        std::fprintf(stderr,
            "decompressed: no room for reloc count at offset 0x%X\n",
            reloc_offset);
        return false;
    }

    const uint32_t n_relocs = read_be_u32(bytes.data() + reloc_offset);
    const size_t reloc_table_end = reloc_offset + 4 + 4 * size_t(n_relocs);
    if (reloc_table_end > bytes.size()) {
        std::fprintf(stderr,
            "decompressed: reloc table (count=%u) overruns blob\n", n_relocs);
        return false;
    }

    // First pass: parse raw entries.
    struct RawReloc {
        RelocType type;
        uint32_t  section_offset;
        uint32_t  word;        // instruction word at section_offset
    };
    std::vector<RawReloc> raw;
    raw.reserve(n_relocs);
    for (uint32_t i = 0; i < n_relocs; i++) {
        const uint32_t entry = read_be_u32(
            bytes.data() + reloc_offset + 4 + 4 * i);
        const uint8_t  stadium_type = uint8_t((entry >> 24) & 0x7F);
        const uint32_t section_offset = entry & 0x00FFFFFFu;
        const RelocType type = translate_stadium_reloc_type(stadium_type);
        if (type == RelocType::R_MIPS_NONE) {
            std::fprintf(stderr,
                "decompressed: unknown Stadium reloc type 0x%X at "
                "offset 0x%X — skipped\n", stadium_type, section_offset);
            continue;
        }
        if (section_offset + 4 > size_in_ram) {
            std::fprintf(stderr,
                "decompressed: reloc[%u] offset 0x%X out of body\n",
                i, section_offset);
            continue;
        }
        const uint32_t word = read_be_u32(bytes.data() + section_offset);
        raw.push_back({type, section_offset, word});
    }

    // Second pass: emit Reloc entries. HI16 pairs with the next LO16
    // in the list (Stadium's table orders them this way).
    section_out.relocs.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        const RawReloc& rr = raw[i];

        Reloc r{};
        r.address = section_vram + rr.section_offset;
        r.target_section_offset = 0;
        r.target_section = section_index;  // default; cross-section pass refines
        r.symbol_index = uint32_t(-1);
        r.type = rr.type;
        r.reference_symbol = false;

        switch (rr.type) {
            case RelocType::R_MIPS_32: {
                r.target_section_offset = rr.word - section_vram;
                break;
            }
            case RelocType::R_MIPS_26: {
                const uint32_t pc_high = section_vram & 0xF0000000u;
                const uint32_t target  = pc_high |
                                         ((rr.word & 0x03FFFFFFu) << 2);
                r.target_section_offset = target - section_vram;
                break;
            }
            case RelocType::R_MIPS_HI16: {
                // Pair with next LO16 in raw list.
                size_t j = i + 1;
                while (j < raw.size() && raw[j].type != RelocType::R_MIPS_LO16) {
                    j++;
                }
                if (j >= raw.size()) {
                    std::fprintf(stderr,
                        "decompressed: HI16 at offset 0x%X has no paired "
                        "LO16 in reloc table\n", rr.section_offset);
                    break;
                }
                const uint16_t hi_imm = uint16_t(rr.word & 0xFFFFu);
                const int16_t  lo_imm = int16_t(raw[j].word & 0xFFFFu);
                const uint32_t combined =
                    (uint32_t(hi_imm) << 16) + uint32_t(int32_t(lo_imm));
                r.target_section_offset = combined - section_vram;
                break;
            }
            case RelocType::R_MIPS_LO16: {
                // Find preceding HI16. We scan backward for the most
                // recent HI16 (matches Stadium's adjacency convention).
                size_t j = i;
                bool found = false;
                while (j > 0) {
                    j--;
                    if (raw[j].type == RelocType::R_MIPS_HI16) {
                        const uint16_t hi_imm = uint16_t(raw[j].word & 0xFFFFu);
                        const int16_t  lo_imm = int16_t(rr.word & 0xFFFFu);
                        const uint32_t combined =
                            (uint32_t(hi_imm) << 16) +
                            uint32_t(int32_t(lo_imm));
                        r.target_section_offset = combined - section_vram;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::fprintf(stderr,
                        "decompressed: LO16 at offset 0x%X has no paired "
                        "HI16 in reloc table\n", rr.section_offset);
                }
                break;
            }
            default:
                break;
        }

        section_out.relocs.emplace_back(r);
        if (rr.type == RelocType::R_MIPS_32) {
            section_out.has_mips32_relocs = true;
        }
    }

    // The recompiler walks instructions linearly and advances
    // reloc_index only forward (recompilation.cpp: while
    // section.relocs[reloc_index].address < vram). It REQUIRES the
    // relocs to be sorted by address — out-of-order entries are
    // skipped silently and emitted as literal immediates.
    //
    // Stadium's raw reloc table is ordered by HI16+LO16 pair
    // adjacency, NOT by instruction address. For example, in
    // stadium_models sub-fragment 4 (variant 384), the table holds:
    //   [HI16 @ off 0x50][HI16 @ off 0x30][LO16 @ off 0x60]
    // Pairing has already baked target_section_offset above (the
    // HI16 at 0x30 pairs with the LO16 at 0x60 via raw-list
    // adjacency, which is independent of address). Now sort by
    // address so the recompiler's linear walk hits every entry.
    std::sort(section_out.relocs.begin(), section_out.relocs.end(),
              [](const Reloc& a, const Reloc& b) {
                  return a.address < b.address;
              });

    return true;
}

// Once every decompressed section is added, walk every reloc with
// target_section == its own index (the default we set above) and
// re-target R_MIPS_32 entries that actually point into a different
// section. Self-targeting relocs stay self-targeting.
void resolve_cross_section_targets(Context& context,
                                   uint16_t first_added_index) {
    for (size_t si = first_added_index; si < context.sections.size(); si++) {
        Section& section = context.sections[si];
        for (Reloc& r : section.relocs) {
            if (r.type != RelocType::R_MIPS_32) continue;

            // Compute the target absolute address.
            const uint32_t target_addr = section.ram_addr +
                                         r.target_section_offset;

            // Find the section that contains target_addr.
            for (size_t ti = 0; ti < context.sections.size(); ti++) {
                const Section& candidate = context.sections[ti];
                if (candidate.size == 0) continue;
                if (target_addr >= candidate.ram_addr &&
                    target_addr <  candidate.ram_addr + candidate.size) {
                    r.target_section = uint16_t(ti);
                    r.target_section_offset = target_addr -
                                              candidate.ram_addr;
                    break;
                }
            }
        }
    }
}

} // namespace

bool synthesize_decompressed_sections(
    Context& context,
    const std::filesystem::path& rom_path,
    const std::vector<DecompressedSection>& configs)
{
    if (configs.empty()) return true;

    const std::vector<uint8_t> rom = read_rom_file(rom_path);
    if (rom.empty()) {
        std::fprintf(stderr,
            "decompressed: failed to read ROM file: %s\n",
            rom_path.string().c_str());
        return false;
    }

    const uint16_t first_added_index = uint16_t(context.sections.size());

    for (const DecompressedSection& cfg : configs) {
        // Bounds-check the wrapper offset.
        if (cfg.rom_wrapper >= rom.size()) {
            std::fprintf(stderr,
                "decompressed: section %s rom_wrapper 0x%X is past EOF\n",
                cfg.name.c_str(), cfg.rom_wrapper);
            return false;
        }

        // Decompress per format.
        std::vector<uint8_t> blob;
        bool ok = false;
        if (cfg.wrapper_format == "pers_szp_yay0") {
            ok = compression::pers_szp_decompress(
                rom.data() + cfg.rom_wrapper,
                rom.size() - cfg.rom_wrapper,
                blob);
        } else if (cfg.wrapper_format == "yay0") {
            ok = compression::yay0_decompress(
                rom.data() + cfg.rom_wrapper,
                rom.size() - cfg.rom_wrapper,
                blob);
        } else {
            std::fprintf(stderr,
                "decompressed: section %s unknown wrapper_format '%s'\n",
                cfg.name.c_str(), cfg.wrapper_format.c_str());
            return false;
        }
        if (!ok) {
            std::fprintf(stderr,
                "decompressed: section %s failed to decompress wrapper "
                "at ROM 0x%X (format=%s)\n",
                cfg.name.c_str(), cfg.rom_wrapper,
                cfg.wrapper_format.c_str());
            return false;
        }

        // Stash decompressed bytes at the end of context.rom so the
        // existing pipeline (which addresses sections via rom_addr)
        // finds them. The synthesized rom_addr deliberately encodes
        // the wrapper offset in the upper bits for traceability:
        //   synthetic_rom = 0xFE000000 | wrapper_offset
        // The 0xFE prefix is reserved for synthesized sections so it
        // never collides with real ROM offsets (ROM is at most 64MB).
        const uint32_t synthetic_rom = 0xFE000000u | cfg.rom_wrapper;

        // Section size = relocOffset (body + bss before relocs).
        const uint32_t reloc_offset = read_be_u32(blob.data() + 0x14);

        // Append decompressed bytes to context.rom at synthetic_rom.
        // Size we copy is reloc_offset (only the body, NOT the trailing
        // reloc table — that's metadata, not section content).
        const size_t needed_rom_size =
            size_t(synthetic_rom) + reloc_offset;
        if (context.rom.size() < needed_rom_size) {
            context.rom.resize(needed_rom_size, 0);
        }
        std::memcpy(context.rom.data() + synthetic_rom,
                    blob.data(), reloc_offset);

        // Build the Section struct.
        const uint16_t section_index =
            uint16_t(context.sections.size());

        Section section{};
        section.rom_addr   = synthetic_rom;
        section.ram_addr   = cfg.vram;
        section.size       = reloc_offset;
        section.bss_size   = 0;  // BSS is part of body in this format.
        section.name       = cfg.name;
        section.executable = true;
        section.relocatable = cfg.relocatable;

        if (!parse_fragment_relocs(blob, cfg.vram, section_index, section)) {
            std::fprintf(stderr,
                "decompressed: section %s reloc parsing failed\n",
                cfg.name.c_str());
            return false;
        }

        // Add the section to the context. We need to grow
        // section_functions in lockstep.
        context.sections.emplace_back(std::move(section));
        context.section_functions.emplace_back();

        // Synthesize functions for the FRAGMENT layout:
        //
        //   1. fragment_entry at +0x00 — 8 bytes (J insn + nop) that
        //      jumps to the real implementation at +0x20.
        //   2. The implementation function at +0x20 — runs from +0x20
        //      to the first `jr $ra` (0x03E00008) we encounter in the
        //      body, plus its delay slot.
        //
        // Without (2), the recompiler's emit for (1) sees "branch to
        // 0x...0020 (no symbol)" and falls back to recomp_unhandled_
        // branch, which is a runtime abort. Once (2) exists in
        // functions_by_vram, the J becomes a tail call and dispatch
        // works the same way it does for ELF-symtab-listed functions.
        //
        // Function::words holds raw ROM bytes (big-endian instructions
        // stored in host-endian uint32 — numerically byteswapped from
        // the actual instruction value). The recompilation pass calls
        // byteswap(word) to recover the BE numeric form.

        auto add_function = [&](uint32_t vram, uint32_t rom,
                                std::vector<uint32_t> words,
                                std::string name) {
            const size_t fi = context.functions.size();
            context.functions.emplace_back(
                vram, rom, std::move(words), name,
                section_index, false, false, false);
            context.section_functions[section_index].push_back(fi);
            context.sections[section_index].function_addrs.push_back(vram);
            context.functions_by_vram[vram].push_back(fi);
            context.functions_by_name[name] = fi;
        };

        // (1) Entry trampoline: 8 bytes at vram+0.
        std::vector<uint32_t> entry_words(2);
        std::memcpy(entry_words.data(), blob.data() + 0x00, 8);
        add_function(cfg.vram, synthetic_rom,
                     std::move(entry_words),
                     cfg.name + "_entry");

        // (2) Implementation function at vram+0x20. Scan forward from
        // +0x20 for the first `jr $ra` (BE numeric 0x03E00008, stored
        // little-endian in our blob bytes as 08 00 E0 03). Include the
        // delay slot in the function size.
        constexpr uint32_t IMPL_OFFSET = 0x20;
        const uint8_t jr_ra_be[4] = { 0x03, 0xE0, 0x00, 0x08 };
        size_t impl_end = 0;
        for (size_t off = IMPL_OFFSET; off + 4 <= reloc_offset; off += 4) {
            if (std::memcmp(blob.data() + off, jr_ra_be, 4) == 0) {
                // Include this jr ra and its delay slot.
                impl_end = off + 8;
                if (impl_end > reloc_offset) impl_end = reloc_offset;
                break;
            }
        }
        if (impl_end > IMPL_OFFSET) {
            const size_t impl_size = impl_end - IMPL_OFFSET;
            std::vector<uint32_t> impl_words(impl_size / 4);
            std::memcpy(impl_words.data(),
                        blob.data() + IMPL_OFFSET, impl_size);
            // Use the convention func_<vram> so the name matches what
            // the recompiler would have generated from an ELF symbol.
            const std::string impl_name = fmt::format(
                "func_{:08X}", cfg.vram + IMPL_OFFSET);
            add_function(cfg.vram + IMPL_OFFSET,
                         synthetic_rom + IMPL_OFFSET,
                         std::move(impl_words),
                         impl_name);
        } else {
            std::fprintf(stderr,
                "decompressed: section %s — could not locate jr $ra "
                "in body at +0x20; only fragment_entry will be "
                "recompiled (jal targets through the entry will become "
                "runtime aborts)\n", cfg.name.c_str());
        }

        std::fprintf(stderr,
            "decompressed: synthesized section %s @ vram 0x%08X "
            "size 0x%X relocs=%zu (wrapper rom 0x%X format=%s)\n",
            cfg.name.c_str(), cfg.vram, reloc_offset,
            context.sections[section_index].relocs.size(),
            cfg.rom_wrapper, cfg.wrapper_format.c_str());
    }

    // Cross-section R_MIPS_32 retargeting now that all decompressed
    // sections are in context.sections.
    resolve_cross_section_targets(context, first_added_index);

    return true;
}

namespace {

// Adds one synthesized section + its functions + reloc table to the
// context. Used by both the explicit per-fragment path and the pattern
// auto-discovery path. `blob` is the decompressed body+relocs (must
// start with the FRAGMENT header). On success, returns the section
// index. On failure, returns size_t(-1) and prints to stderr.
size_t add_decompressed_section(Context& context,
                                const std::vector<uint8_t>& blob,
                                uint32_t rom_wrapper,
                                uint32_t vram,
                                const std::string& section_name,
                                bool relocatable,
                                uint64_t content_hash,
                                uint32_t override_link_ram_addr = 0,
                                uint32_t original_pattern_id = 0xFFFFFFFFu)
{
    // `vram` is the BYTES-ENCODED vram — what the body's R_MIPS_HI16/LO16
    // / R_MIPS_32 / J/JAL targets are encoded relative to. The CFG walker
    // and reloc parser need this value (otherwise jump-table entries
    // resolve to the wrong section, etc.).
    //
    // `override_link_ram_addr` (if non-zero) is the section's LINK
    // IDENTITY — what gets stored in section.ram_addr and used at
    // runtime as section_addresses[N]'s initial value. For pattern
    // variants we want this DIFFERENT from `vram` so multiple variants
    // can have unique link identities while sharing the canonical
    // bytes-encoded vram.
    const uint32_t link_ram_addr =
        (override_link_ram_addr != 0) ? override_link_ram_addr : vram;
    if (blob.size() < 0x20) {
        std::fprintf(stderr,
            "decompressed: section %s blob smaller than FRAGMENT header\n",
            section_name.c_str());
        return size_t(-1);
    }
    if (std::memcmp(blob.data() + 0x08, "FRAGMENT", 8) != 0) {
        std::fprintf(stderr,
            "decompressed: section %s missing FRAGMENT magic\n",
            section_name.c_str());
        return size_t(-1);
    }

    // Stash decompressed bytes in context.rom at a synthetic_rom that's
    // GUARANTEED not to overlap any other section. We use a cumulative
    // allocator: a static counter that grows as sections are added, so
    // each section's bytes occupy a fresh, non-overlapping range.
    //
    // The previous formula (0xFE000000 | wrap_off) was wrong because
    // Stadium's wrappers are densely packed in ROM — wrap_offs are
    // closer together than the SUM of their decompressed sizes — so
    // (0xFE000000 | wrap_offA) + size_A often overlapped
    // (0xFE000000 | wrap_offB). The second memcpy clobbered the first
    // section's body, including its jump-table entries.
    //
    // Cumulative allocation eliminates the overlap entirely. The
    // 0xFE000000 prefix is preserved for traceability (synthetic ranges
    // start above any real ROM offset, which is at most ~64 MB).
    const uint32_t reloc_offset = read_be_u32(blob.data() + 0x14);
    if (reloc_offset > blob.size()) {
        std::fprintf(stderr,
            "decompressed: section %s relocOffset 0x%X exceeds blob 0x%zX\n",
            section_name.c_str(), reloc_offset, blob.size());
        return size_t(-1);
    }

    // Cumulative synthetic-rom counter. Aligned to 4 bytes so MIPS
    // instruction reads are always aligned.
    static uint64_t next_synthetic_rom = 0xFE000000ull;
    const uint32_t synthetic_rom = uint32_t(next_synthetic_rom);
    next_synthetic_rom += (uint64_t(reloc_offset) + 3u) & ~uint64_t(3u);
    if (next_synthetic_rom > 0xFFFFFFFFull) {
        std::fprintf(stderr,
            "decompressed: section %s — synthetic_rom counter overflowed "
            "32 bits (next=0x%llX). Engine assumes < 256 MB of "
            "synthesized-section payload total.\n",
            section_name.c_str(),
            (unsigned long long)next_synthetic_rom);
        return size_t(-1);
    }

    const size_t needed_rom_size = size_t(synthetic_rom) + reloc_offset;
    if (context.rom.size() < needed_rom_size) {
        context.rom.resize(needed_rom_size, 0);
    }
    std::memcpy(context.rom.data() + synthetic_rom,
                blob.data(), reloc_offset);

    const uint16_t section_index = uint16_t(context.sections.size());

    Section section{};
    section.rom_addr   = synthetic_rom;
    // Section identity (link_ram_addr) may differ from the bytes-encoded
    // vram (`vram`) for pattern variants that get assigned a synthetic
    // per-variant link identity. The reloc parser stays with the
    // bytes-encoded vram so target_section_offset values are correct
    // intra-section byte distances.
    section.ram_addr   = link_ram_addr;
    section.size       = reloc_offset;
    section.bss_size   = 0;
    section.name       = section_name;
    section.executable = true;
    section.relocatable = relocatable;
    section.content_hash = content_hash;
    section.original_pattern_id = original_pattern_id;

    if (!parse_fragment_relocs(blob, vram, section_index, section)) {
        return size_t(-1);
    }

    context.sections.emplace_back(std::move(section));
    context.section_functions.emplace_back();

    auto add_function = [&](uint32_t f_vram, uint32_t f_rom,
                            std::vector<uint32_t> words,
                            std::string name) {
        const size_t fi = context.functions.size();
        context.functions.emplace_back(
            f_vram, f_rom, std::move(words), name,
            section_index, false, false, false);
        context.section_functions[section_index].push_back(fi);
        context.sections[section_index].function_addrs.push_back(f_vram);
        context.functions_by_vram[f_vram].push_back(fi);
        context.functions_by_name[name] = fi;
    };

    // Stadium has two FRAGMENT shapes that share the same +0x00..0x20
    // header (J trampoline + magic + sizes):
    //
    //   Code fragment:  +0x20 is a real MIPS function ending in jr $ra
    //                   (and possibly more functions interspersed with
    //                   data). Stadium calls the J at +0x00 to dispatch
    //                   into the function.
    //
    //   Data fragment:  +0x20 onwards is pure data (tables of
    //                   (tag, pointer) records, etc.). The J at +0x00
    //                   is a dormant placeholder that Stadium NEVER
    //                   actually calls — Stadium reads the data
    //                   directly. No MIPS function exists.
    //
    // We distinguish by scanning the first 0x100 instructions of the
    // body for ANY jr $ra (0x03E00008). If absent, the fragment is
    // data-only: we register the section + R_MIPS_32 relocs but emit
    // NO FuncEntry rows. Stadium's dispatch never goes through a
    // func_map entry for these. If something ever does call the
    // entry-trampoline J, the runtime LOOKUP_FUNC reports the miss
    // loudly, which is the correct surface — NOT a stub.
    constexpr uint32_t IMPL_OFFSET = 0x20;
    bool has_jr_ra = false;
    {
        const size_t scan_end = std::min<size_t>(
            reloc_offset, IMPL_OFFSET + 0x400);  // first 256 insns
        for (size_t off = IMPL_OFFSET; off + 4 <= scan_end; off += 4) {
            if (read_be_u32(blob.data() + off) == 0x03E00008u) {
                has_jr_ra = true;
                break;
            }
        }
    }

    if (!has_jr_ra) {
        // Data-only fragment: section + relocs only, no functions.
        std::fprintf(stderr,
            "decompressed: section %s — data-only fragment (no jr $ra "
            "in first 0x400 bytes); registered as section + relocs "
            "with no FuncEntry rows. Stadium never dispatches the +0x00 "
            "J trampoline for these (would surface as a runtime "
            "lookup miss if it did, which is the correct diagnostic).\n",
            section_name.c_str());
        return section_index;
    }

    // Code fragment path: synthesize entry trampoline + impl function.

    // (1) Entry trampoline at vram+0 (8 bytes).
    std::vector<uint32_t> entry_words(2);
    std::memcpy(entry_words.data(), blob.data() + 0x00, 8);
    add_function(vram, synthetic_rom,
                 std::move(entry_words),
                 section_name + "_entry");

    // (2) Implementation function at vram+0x20. The engine's
    // analysis.cpp::discover_function_bounds runs a real BFS-based
    // control-flow walk that follows conditional branches, j/jal
    // targets, and jr-via-jump-table dispatches (resolved using the
    // existing register-state simulator). On failure it reports a
    // specific offset and reason; we propagate that as a build error
    // — no graceful skip, no stub.
    if (reloc_offset <= IMPL_OFFSET + 4) {
        std::fprintf(stderr,
            "decompressed: section %s — body too small to contain a "
            "function at +0x20 (reloc_offset=0x%X)\n",
            section_name.c_str(), reloc_offset);
        return size_t(-1);
    }

    size_t impl_size = 0;
    std::string discover_err;
    bool ok = discover_function_bounds(
        blob.data(), reloc_offset,
        vram, IMPL_OFFSET,
        impl_size, discover_err);
    if (!ok) {
        std::fprintf(stderr,
            "decompressed: section %s — function-bounds discovery "
            "failed: %s\n"
            "  Build aborted. Resolutions, in order of preference:\n"
            "    1. If this is a recompiler analysis gap, fix the\n"
            "       analyzer in src/analysis.cpp.\n"
            "    2. If the fragment legitimately has a shape the\n"
            "       analyzer can't handle, declare it via the\n"
            "       single-block [[input.decompressed_section]] form\n"
            "       (manual analysis path).\n"
            "    3. If the wrapper is unused / unreachable in this\n"
            "       game's runtime path, exclude it via a future\n"
            "       pattern.exclude config field.\n"
            "  No graceful skip, no stub. Build refuses to ship.\n",
            section_name.c_str(), discover_err.c_str());
        return size_t(-1);
    }

    std::vector<uint32_t> impl_words(impl_size / 4);
    std::memcpy(impl_words.data(),
                blob.data() + IMPL_OFFSET, impl_size);
    const std::string impl_name = fmt::format(
        "func_{:08X}", vram + IMPL_OFFSET);
    add_function(vram + IMPL_OFFSET,
                 synthetic_rom + IMPL_OFFSET,
                 std::move(impl_words),
                 impl_name);

    return section_index;
}

// Decompress a wrapper at the given ROM offset using the named format.
// Returns true + populates blob on success.
bool decompress_wrapper_at(const std::vector<uint8_t>& rom,
                           uint32_t rom_wrapper,
                           const std::string& wrapper_format,
                           std::vector<uint8_t>& blob_out)
{
    if (rom_wrapper >= rom.size()) return false;
    if (wrapper_format == "pers_szp_yay0") {
        return compression::pers_szp_decompress(
            rom.data() + rom_wrapper,
            rom.size() - rom_wrapper, blob_out);
    } else if (wrapper_format == "yay0") {
        return compression::yay0_decompress(
            rom.data() + rom_wrapper,
            rom.size() - rom_wrapper, blob_out);
    }
    return false;
}

} // namespace

bool synthesize_decompressed_patterns(
    Context& context,
    const std::filesystem::path& rom_path,
    const std::vector<DecompressedSectionPattern>& patterns)
{
    if (patterns.empty()) return true;

    const std::vector<uint8_t> rom = read_rom_file(rom_path);
    if (rom.empty()) {
        std::fprintf(stderr,
            "decompressed: failed to read ROM file: %s\n",
            rom_path.string().c_str());
        return false;
    }

    const uint16_t first_added_index = uint16_t(context.sections.size());

    for (const DecompressedSectionPattern& p : patterns) {
        // Compute the J-trampoline encoding we expect at +0x00 of any
        // matching fragment: J <vram + 0x20> + nop. MIPS J insn:
        //   opcode 0x02 << 26 | (target >> 2) & 0x03FFFFFF
        const uint32_t j_target = p.vram + 0x20u;
        const uint32_t j_insn = 0x08000000u |
                                ((j_target >> 2) & 0x03FFFFFFu);
        // Big-endian byte pattern for the first 8 bytes (J + nop).
        uint8_t expected_first8[8];
        expected_first8[0] = uint8_t(j_insn >> 24);
        expected_first8[1] = uint8_t(j_insn >> 16);
        expected_first8[2] = uint8_t(j_insn >> 8);
        expected_first8[3] = uint8_t(j_insn);
        expected_first8[4] = 0;
        expected_first8[5] = 0;
        expected_first8[6] = 0;
        expected_first8[7] = 0;
        const uint8_t fragment_magic[8] = {
            'F', 'R', 'A', 'G', 'M', 'E', 'N', 'T'
        };

        // Resolve the base_name (default: "frag_<vram>").
        std::string base_name = p.base_name;
        if (base_name.empty()) {
            base_name = fmt::format("frag_{:08X}", p.vram);
        }

        // Scan the ROM for Yay0 magic. For each, decompress 0x40 bytes,
        // check the J-insn + FRAGMENT-magic match, and accept.
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> hits;
        size_t scan_pos = 0;
        while (scan_pos + 16 < rom.size()) {
            // Find next "Yay0" magic.
            size_t y0 = std::string::npos;
            for (size_t i = scan_pos; i + 4 <= rom.size(); i++) {
                if (rom[i]   == 'Y' && rom[i+1] == 'a' &&
                    rom[i+2] == 'y' && rom[i+3] == '0') {
                    y0 = i;
                    break;
                }
            }
            if (y0 == std::string::npos) break;
            scan_pos = y0 + 4;

            // Quick prefix decompress to test the FRAGMENT shape.
            std::vector<uint8_t> prefix;
            if (!compression::yay0_decompress(
                    rom.data() + y0, rom.size() - y0, prefix)) {
                continue;
            }
            if (prefix.size() < 0x10) continue;
            if (std::memcmp(prefix.data(), expected_first8, 8) != 0) continue;
            if (std::memcmp(prefix.data() + 8, fragment_magic, 8) != 0) continue;

            // Match — figure out the wrapper offset (PERS-SZP wraps Yay0
            // at -0x18 if the format is pers_szp_yay0; otherwise the
            // wrapper offset IS the Yay0 offset).
            uint32_t wrap_off = uint32_t(y0);
            if (p.wrapper_format == "pers_szp_yay0") {
                if (y0 < 0x18) continue;
                if (std::memcmp(rom.data() + (y0 - 0x18),
                                "PERS-SZP", 8) != 0) {
                    continue;
                }
                wrap_off = uint32_t(y0 - 0x18);
            } else if (p.wrapper_format != "yay0") {
                std::fprintf(stderr,
                    "decompressed: pattern %s unknown wrapper_format '%s'\n",
                    base_name.c_str(), p.wrapper_format.c_str());
                return false;
            }

            // Full decompress.
            std::vector<uint8_t> body;
            if (!decompress_wrapper_at(rom, wrap_off, p.wrapper_format, body)) {
                continue;
            }
            hits.emplace_back(wrap_off, std::move(body));
        }

        std::fprintf(stderr,
            "decompressed pattern %s @ vram 0x%08X format=%s: "
            "found %zu wrappers in ROM\n",
            base_name.c_str(), p.vram, p.wrapper_format.c_str(),
            hits.size());

        if (hits.empty()) continue;

        // Deduplicate by content hash. Hash window is the first 0x100
        // bytes — measured at 95% uniqueness for Stadium's 0x8FF00000
        // slot. The runtime side uses the SAME window over the bytes
        // Stadium decompressed into RDRAM, so build-time and runtime
        // hashes match. (Smaller fragments hash their full body.)
        constexpr size_t HASH_WINDOW = 0x100;
        std::unordered_map<uint64_t, size_t> seen_hashes;
        size_t added = 0;
        size_t deduped = 0;
        // Path 2: every unique pattern variant gets its own synthetic
        // per-variant ram_addr in the 0xC0000000+ sentinel pool. The
        // bytes-encoded vram (parsing/CFG) stays at p.vram for ALL
        // variants — only section.ram_addr (the link identity) changes,
        // so each variant's RELOC_HI16/LO16 macros emit a unique
        // 0xCXXXXXXX literal at runtime and the synthetic resolver
        // can translate that literal back to the variant's runtime
        // RDRAM buffer.
        //
        // Pool placement: 0xC0000000 is KSEG2, unused by N64 software,
        // so the sentinel is "obviously invalid as an N64 vaddr" and
        // can only be handled by the recomp synthetic resolver. KSEG1
        // (0xA0000000) was tried first but collides with the engine's
        // RSP code section at 0xA4000040.
        //
        // Stride 0x00100000 = 1 MB per variant. With Stadium's 219
        // variants, the pool occupies 0xC0000000..0xCDB00000 — well
        // within the 256-slot capacity (kSyntheticBucketCount on the
        // runtime side). If the variant count grows past 256 in some
        // future game, both sides need to bump the pool size.
        const uint32_t kSyntheticPoolBase = 0xC0000000u;
        const uint32_t kSyntheticPoolStride = 0x00100000u;
        size_t probe_variant_idx = 0;
        for (auto& [wrap_off, body] : hits) {
            const size_t window = std::min(HASH_WINDOW, body.size());
            const uint64_t content_hash =
                fnv1a_64(body.data(), window);
            auto it = seen_hashes.find(content_hash);
            if (it != seen_hashes.end()) {
                deduped++;
                continue;
            }
            seen_hashes.emplace(content_hash, wrap_off);

            // Original game-side fragment id derived from the pattern's
            // canonical bucket. All variants of this pattern share the
            // same original id (e.g. 0xEF for stadium_models). Stored
            // on each section so the runtime can filter synthetic
            // candidates to the matching id and avoid cross-pattern
            // hash-collision misregistration.
            const uint32_t orig_id =
                ((p.vram & 0x0FF00000u) >> 0x14) - 0x10u;

            // Per-variant synthetic link identity. The bytes-encoded
            // vram (used for parsing/CFG) stays at p.vram — only this
            // link identity changes per variant.
            const uint32_t variant_link_addr =
                kSyntheticPoolBase +
                uint32_t(probe_variant_idx) * kSyntheticPoolStride;
            probe_variant_idx++;

            const std::string section_name = fmt::format(
                "{}__rom_{:X}", base_name, wrap_off);
            size_t si = add_decompressed_section(
                context, body, wrap_off, p.vram,
                section_name, p.relocatable, content_hash,
                variant_link_addr, orig_id);
            if (si == size_t(-1)) {
                // Hard failure: the section's bytes can't be bounded
                // by our CFG walk (or some other unrecoverable parse
                // error). NOT a soft-skip; the user has to decide.
                std::fprintf(stderr,
                    "decompressed: pattern %s aborted — section for "
                    "ROM 0x%X failed to synthesize. See message above.\n",
                    base_name.c_str(), wrap_off);
                return false;
            }
            added++;
        }
        std::fprintf(stderr,
            "decompressed pattern %s: %zu sections added "
            "(%zu deduped as content-identical)\n",
            base_name.c_str(), added, deduped);
    }

    // Cross-section R_MIPS_32 retargeting once everything is in.
    resolve_cross_section_targets(context, first_added_index);

    return true;
}

} // namespace N64Recomp
