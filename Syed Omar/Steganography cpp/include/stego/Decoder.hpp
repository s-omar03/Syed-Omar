#ifndef STEGO_DECODER_HPP
#define STEGO_DECODER_HPP

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "stego/Types.hpp"

namespace stego {

/// Recovers a secret file previously hidden by Encoder.
class Decoder {
public:
    Decoder() = default;

    /// Parses `args` (the full argv as strings).
    /// Expected shape: {prog, "-d", <stego.bmp>, [output-name-without-extension]}
    [[nodiscard]] Status readAndValidateArgs(const std::vector<std::string>& args);

    /// Runs the whole decode pipeline. Call after readAndValidateArgs().
    [[nodiscard]] Status doDecoding();

    /// Full name of the recovered file, valid once decoding has succeeded.
    [[nodiscard]] const std::string& secretFileName() const noexcept { return secretFileName_; }

private:
    [[nodiscard]] Status openImageFile();
    [[nodiscard]] Status skipBmpHeader();
    [[nodiscard]] Status decodeMagicString();
    [[nodiscard]] Status decodeSecretFileExtnSize();
    [[nodiscard]] Status decodeSecretFileExtn();
    [[nodiscard]] Status decodeSecretFileSize();
    [[nodiscard]] Status decodeSecretFileData();

    /// Reads `count` hidden bytes, consuming 8 stego-image bytes each.
    [[nodiscard]] Status decodeStringFromImage(std::size_t count, std::string& out);

    /// Reads one hidden 32-bit value, consuming 32 stego-image bytes.
    [[nodiscard]] Status decodeIntFromImage(std::uint32_t& out);

    std::string stegoImageName_;
    std::string secretFileStem_{kDefaultSecretStem};
    std::string secretFileName_;
    std::string secretExtension_;

    std::ifstream stegoImage_;
    std::ofstream secretFile_;

    std::uint32_t secretFileSize_{0};
    std::uint32_t secretExtensionSize_{0};
};

} // namespace stego

#endif // STEGO_DECODER_HPP
