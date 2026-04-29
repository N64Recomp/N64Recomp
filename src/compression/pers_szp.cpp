#include "compression/pers_szp.h"
#include "compression/yay0.h"

#include <cstring>

namespace N64Recomp::compression {

static uint32_t read_be_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

bool pers_szp_decompress(const uint8_t* data, size_t data_size,
                         std::vector<uint8_t>& out) {
    if (data_size < 0x18) {
        return false;
    }
    if (std::memcmp(data, "PERS-SZP", 8) != 0) {
        return false;
    }

    const uint32_t payload_off    = read_be_u32(data + 0x08);
    const uint32_t decomp_size_a  = read_be_u32(data + 0x0C);
    const uint32_t decomp_size_b  = read_be_u32(data + 0x10);
    if (payload_off != 0x18 || decomp_size_a != decomp_size_b) {
        return false;
    }
    if (payload_off >= data_size) {
        return false;
    }

    if (!yay0_decompress(data + payload_off, data_size - payload_off, out)) {
        return false;
    }

    if (out.size() != decomp_size_a) {
        return false;
    }
    return true;
}

} // namespace N64Recomp::compression
