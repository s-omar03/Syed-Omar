#include "stego/LsbCodec.hpp"

#include <cassert>

namespace stego {

void LsbCodec::encodeByte(std::uint8_t value, std::span<char> buffer) {
    assert(buffer.size() >= kBitsPerByte);
    for (std::size_t i = 0; i < kBitsPerByte; ++i) {
        const unsigned shift = static_cast<unsigned>(kBitsPerByte - 1 - i);
        const unsigned bit = (value >> shift) & 1U;
        auto byte = static_cast<std::uint8_t>(buffer[i]);
        byte = static_cast<std::uint8_t>((byte & 0xFEU) | bit);
        buffer[i] = static_cast<char>(byte);
    }
}

std::uint8_t LsbCodec::decodeByte(std::span<const char> buffer) {
    assert(buffer.size() >= kBitsPerByte);
    std::uint8_t value = 0;
    for (std::size_t i = 0; i < kBitsPerByte; ++i) {
        const unsigned bit = static_cast<std::uint8_t>(buffer[i]) & 1U;
        value = static_cast<std::uint8_t>((value << 1) | bit);
    }
    return value;
}

void LsbCodec::encodeInt(std::uint32_t value, std::span<char> buffer) {
    assert(buffer.size() >= kBitsPerInt);
    for (std::size_t i = 0; i < kBitsPerInt; ++i) {
        const unsigned shift = static_cast<unsigned>(kBitsPerInt - 1 - i);
        const unsigned bit = (value >> shift) & 1U;
        auto byte = static_cast<std::uint8_t>(buffer[i]);
        byte = static_cast<std::uint8_t>((byte & 0xFEU) | bit);
        buffer[i] = static_cast<char>(byte);
    }
}

std::uint32_t LsbCodec::decodeInt(std::span<const char> buffer) {
    assert(buffer.size() >= kBitsPerInt);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < kBitsPerInt; ++i) {
        const unsigned bit = static_cast<std::uint8_t>(buffer[i]) & 1U;
        value = (value << 1) | bit;
    }
    return value;
}

} // namespace stego
