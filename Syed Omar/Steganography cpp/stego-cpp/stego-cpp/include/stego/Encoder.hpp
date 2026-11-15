#ifndef STEGO_ENCODER_HPP
#define STEGO_ENCODER_HPP

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "stego/Types.hpp"

namespace stego {

/// Hides a secret file inside the pixel data of a 24-bit BMP image.
///
/// Layout written after the 54-byte BMP header, MSB-first, one bit per
/// image byte:
///   magic string | extension length (32 bits) | extension | file size
///   (32 bits) | file bytes | untouched remainder of the image
class Encoder {
public:
    Encoder() = default;

    /// Parses `args` (the full argv as strings) and checks extensions.
    /// Expected shape: {prog, "-e", <src.bmp>, <secret.txt>, [stego.bmp]}
    [[nodiscard]] Status readAndValidateArgs(const std::vector<std::string>& args);

    /// Runs the whole encode pipeline. Call after readAndValidateArgs().
    [[nodiscard]] Status doEncoding();

    [[nodiscard]] const std::string& stegoImageName() const noexcept { return stegoImageName_; }

private:
    [[nodiscard]] Status openFiles();
    [[nodiscard]] Status checkCapacity();
    [[nodiscard]] Status copyBmpHeader();
    [[nodiscard]] Status encodeMagicString();
    [[nodiscard]] Status encodeSecretFileExtnSize();
    [[nodiscard]] Status encodeSecretFileExtn();
    [[nodiscard]] Status encodeSecretFileSize();
    [[nodiscard]] Status encodeSecretFileData();
    [[nodiscard]] Status copyRemainingImageData();

    /// Encodes `data` one byte at a time, consuming 8 source-image bytes each.
    [[nodiscard]] Status encodeDataToImage(std::string_view data);

    /// Encodes a 32-bit length field, consuming 32 source-image bytes.
    [[nodiscard]] Status encodeIntToImage(std::uint32_t value);

    std::string srcImageName_;
    std::string secretFileName_;
    std::string stegoImageName_{kDefaultStegoImage};
    std::string secretExtension_;

    std::ifstream srcImage_;
    std::ifstream secretFile_;
    std::ofstream stegoImage_;

    std::uint32_t imageCapacity_{0};
    std::uint32_t secretFileSize_{0};
};

} // namespace stego

#endif // STEGO_ENCODER_HPP
