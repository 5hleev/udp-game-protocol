// crc32.h - CRC-32 (IEEE 802.3, полином 0xEDB88320, как в zlib/Ethernet)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace crc {

inline const std::array<uint32_t, 256>& table() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> res{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            res[i] = c;
        }
        return res;
    }();
    return t;
}

// Табличный CRC32. Контрольное значение: crc32("123456789") == 0xCBF43926
inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t prev = 0) {
    uint32_t c = ~prev;
    const auto& t = table();
    for (size_t i = 0; i < len; ++i)
        c = t[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

} // namespace crc
