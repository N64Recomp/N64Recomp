#ifndef N64RECOMP_COMPRESSION_YAY0_H
#define N64RECOMP_COMPRESSION_YAY0_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace N64Recomp::compression {

// Decompresses a Yay0-encoded stream.
//
// Layout (big-endian):
//   +0x00  "Yay0"                4 bytes magic
//   +0x04  uint32 decomp_size    decompressed output size
//   +0x08  uint32 link_table_off offset to 16-bit back-reference table
//   +0x0C  uint32 data_off       offset to literal byte stream
//   +0x10..link_table_off-1      packed control bitstream (32-bit words,
//                                MSB first; 1 = literal byte, 0 = back-ref)
//
// `data` must point at the magic; `data_size` is the bytes available
// from that point to end-of-buffer (used for bounds checking).
//
// Returns true on success; out is resized to decomp_size.
// Returns false if the magic is wrong, the buffer is truncated, or the
// stream produces an output exceeding decomp_size.
bool yay0_decompress(const uint8_t* data, size_t data_size,
                     std::vector<uint8_t>& out);

} // namespace N64Recomp::compression

#endif
