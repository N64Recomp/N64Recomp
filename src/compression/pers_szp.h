#ifndef N64RECOMP_COMPRESSION_PERS_SZP_H
#define N64RECOMP_COMPRESSION_PERS_SZP_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace N64Recomp::compression {

// Decompresses a PERS-SZP-wrapped Yay0 stream.
//
// PERS-SZP layout (big-endian):
//   +0x00  "PERS-SZP\0"          9 bytes magic, padded to 16
//   +0x08  uint32 payload_off    typically 0x18
//   +0x0C  uint32 decomp_size    decompressed output size (duplicated below)
//   +0x10  uint32 decomp_size    same value (sanity check)
//   +0x14  uint32 reserved       0
//   +0x18  Yay0 stream begins (see yay0.h)
//
// `data` points at the PERS-SZP magic; `data_size` is bytes available.
// Returns true on success; sets out to decompressed bytes.
bool pers_szp_decompress(const uint8_t* data, size_t data_size,
                         std::vector<uint8_t>& out);

} // namespace N64Recomp::compression

#endif
