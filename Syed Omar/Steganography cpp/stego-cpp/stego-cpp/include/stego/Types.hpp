#ifndef STEGO_TYPES_HPP
#define STEGO_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace stego {

/// Result of an operation. Scoped so it cannot implicitly convert to int.
enum class Status {
    Success,
    Failure
};

/// Which mode the program was invoked in.
enum class OperationType {
    Encode,
    Decode,
    Unsupported
};

/// Signature written into the image so the decoder can tell a stego image
/// apart from an untouched one.
inline constexpr std::string_view kMagicString = "#*";

/// Size of the BMP header (14-byte file header + 40-byte DIB header).
inline constexpr std::size_t kBmpHeaderSize = 54;

/// Every hidden byte consumes 8 image bytes (one bit per LSB).
inline constexpr std::size_t kBitsPerByte = 8;

/// A 32-bit length field consumes 32 image bytes.
inline constexpr std::size_t kBitsPerInt = 32;

/// Name used for the recovered secret file when the user gives no output name.
inline constexpr std::string_view kDefaultSecretStem = "decoded_output";

/// Name used for the stego image when the user gives no output name.
inline constexpr std::string_view kDefaultStegoImage = "stego.bmp";

} // namespace stego

#endif // STEGO_TYPES_HPP
