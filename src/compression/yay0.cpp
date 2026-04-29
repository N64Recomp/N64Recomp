#include "compression/yay0.h"

#include <cstring>

namespace N64Recomp::compression {

static uint32_t read_be_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

static uint16_t read_be_u16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

bool yay0_decompress(const uint8_t* data, size_t data_size,
                     std::vector<uint8_t>& out) {
    if (data_size < 16) {
        return false;
    }
    if (std::memcmp(data, "Yay0", 4) != 0) {
        return false;
    }

    const uint32_t decomp_size = read_be_u32(data + 4);
    const uint32_t link_table_off = read_be_u32(data + 8);
    const uint32_t data_off = read_be_u32(data + 12);

    if (link_table_off >= data_size || data_off >= data_size) {
        return false;
    }

    out.assign(decomp_size, 0);

    size_t out_pos = 0;
    size_t cmd_pos = 16;
    size_t link_pos = link_table_off;
    size_t data_pos = data_off;
    uint32_t cmd = 0;
    int cmd_bits = 0;

    while (out_pos < decomp_size) {
        if (cmd_bits == 0) {
            if (cmd_pos + 4 > data_size) return false;
            cmd = read_be_u32(data + cmd_pos);
            cmd_pos += 4;
            cmd_bits = 32;
        }
        if (cmd & 0x80000000u) {
            // Literal byte from the data stream.
            if (data_pos >= data_size) return false;
            out[out_pos++] = data[data_pos++];
        } else {
            // Back-reference: 16-bit link word + optional 8-bit count.
            if (link_pos + 2 > data_size) return false;
            const uint16_t link = read_be_u16(data + link_pos);
            link_pos += 2;

            const uint32_t offset = link & 0x0FFFu;
            uint32_t count = link >> 12;
            if (count == 0) {
                if (data_pos >= data_size) return false;
                count = uint32_t(data[data_pos++]) + 0x12u;
            } else {
                count += 2;
            }

            // ref_pos is signed-safe because `out_pos > offset` is
            // implied by valid Yay0 streams; bail otherwise.
            if (offset + 1 > out_pos) return false;
            size_t ref_pos = out_pos - offset - 1;
            for (uint32_t i = 0; i < count; i++) {
                if (out_pos >= decomp_size) return false;
                out[out_pos++] = out[ref_pos++];
            }
        }
        cmd <<= 1;
        cmd_bits -= 1;
    }

    return out_pos == decomp_size;
}

} // namespace N64Recomp::compression
