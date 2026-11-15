#ifndef STEGO_LSBCODEC_HPP
#define STEGO_LSBCODEC_HPP

#include <cstdint>
#include <span>

#include "stego/Types.hpp"

namespace stego {

/// Stateless helpers that move data in and out of the least significant bits
/// of an image-byte buffer. Kept separate from Encoder/Decoder so the bit
/// twiddling can be unit tested on its own.
class LsbCodec {
public:
    LsbCodec() = delete;

    /// Writes the 8 bits of `value` into the LSB of each byte of `buffer`,
    /// MSB first. `buffer` must hold exactly 8 bytes.
    static void encodeByte(std::uint8_t value, std::span<char> buffer);

    /// Reads 8 LSBs from `buffer` and reassembles the original byte.
    static std::uint8_t decodeByte(std::span<const char> buffer);

    /// Writes the 32 bits of `value` into the LSB of each byte of `buffer`,
    /// MSB first. `buffer` must hold exactly 32 bytes.
    static void encodeInt(std::uint32_t value, std::span<char> buffer);

    /// Reads 32 LSBs from `buffer` and reassembles the original value.
    static std::uint32_t decodeInt(std::span<const char> buffer);
};

} // namespace stego

#endif // STEGO_LSBCODEC_HPP
