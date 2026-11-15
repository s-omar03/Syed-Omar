#include "stego/Decoder.hpp"

#include <array>
#include <filesystem>
#include <iostream>

#include "stego/LsbCodec.hpp"

namespace stego {
namespace {

/// Guards against a corrupt or non-stego image claiming an absurd length.
constexpr std::uint32_t kMaxExtensionSize = 16;
constexpr std::uint32_t kMaxSecretFileSize = 64U * 1024U * 1024U;

} // namespace

Status Decoder::readAndValidateArgs(const std::vector<std::string>& args) {
    // args[0] = program, args[1] = "-d", args[2] = stego image,
    // args[3] = optional output name without extension.
    if (args.size() < 3) {
        std::cerr << "ERROR: Decoding needs a stego .bmp file.\n";
        return Status::Failure;
    }

    if (std::filesystem::path(args[2]).extension() != ".bmp") {
        std::cerr << "ERROR: Stego image file must have a .bmp extension.\n";
        return Status::Failure;
    }
    stegoImageName_ = args[2];

    if (args.size() < 4) {
        secretFileStem_ = kDefaultSecretStem;
    } else {
        if (std::filesystem::path(args[3]).has_extension()) {
            std::cerr << "ERROR: Output file must not have an extension; "
                         "it is recovered from the image.\n";
            return Status::Failure;
        }
        secretFileStem_ = args[3];
    }

    return Status::Success;
}

Status Decoder::openImageFile() {
    stegoImage_.open(stegoImageName_, std::ios::binary);
    if (!stegoImage_) {
        std::cerr << "ERROR: Unable to open file " << stegoImageName_ << '\n';
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::skipBmpHeader() {
    stegoImage_.seekg(static_cast<std::streamoff>(kBmpHeaderSize), std::ios::beg);
    if (!stegoImage_) {
        std::cerr << "ERROR: Unable to skip BMP header.\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::decodeStringFromImage(std::size_t count, std::string& out) {
    std::array<char, kBitsPerByte> buffer{};
    out.clear();
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        if (!stegoImage_.read(buffer.data(), buffer.size())) {
            std::cerr << "ERROR: Stego image ended unexpectedly.\n";
            return Status::Failure;
        }
        out.push_back(static_cast<char>(LsbCodec::decodeByte(buffer)));
    }
    return Status::Success;
}

Status Decoder::decodeIntFromImage(std::uint32_t& out) {
    std::array<char, kBitsPerInt> buffer{};
    if (!stegoImage_.read(buffer.data(), buffer.size())) {
        std::cerr << "ERROR: Stego image ended unexpectedly.\n";
        return Status::Failure;
    }
    out = LsbCodec::decodeInt(buffer);
    return Status::Success;
}

Status Decoder::decodeMagicString() {
    std::string magic;
    if (decodeStringFromImage(kMagicString.size(), magic) != Status::Success) {
        return Status::Failure;
    }
    if (magic != kMagicString) {
        std::cerr << "ERROR: Magic string mismatch. This image holds no hidden data.\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::decodeSecretFileExtnSize() {
    if (decodeIntFromImage(secretExtensionSize_) != Status::Success) {
        return Status::Failure;
    }
    if (secretExtensionSize_ == 0 || secretExtensionSize_ > kMaxExtensionSize) {
        std::cerr << "ERROR: Decoded extension length looks corrupt ("
                  << secretExtensionSize_ << ").\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::decodeSecretFileExtn() {
    if (decodeStringFromImage(secretExtensionSize_, secretExtension_) != Status::Success) {
        return Status::Failure;
    }

    secretFileName_ = secretFileStem_ + secretExtension_;

    secretFile_.open(secretFileName_, std::ios::binary | std::ios::trunc);
    if (!secretFile_) {
        std::cerr << "ERROR: Unable to create output file " << secretFileName_ << '\n';
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::decodeSecretFileSize() {
    if (decodeIntFromImage(secretFileSize_) != Status::Success) {
        return Status::Failure;
    }
    if (secretFileSize_ == 0 || secretFileSize_ > kMaxSecretFileSize) {
        std::cerr << "ERROR: Decoded file size looks corrupt ("
                  << secretFileSize_ << ").\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::decodeSecretFileData() {
    std::string payload;
    if (decodeStringFromImage(secretFileSize_, payload) != Status::Success) {
        return Status::Failure;
    }
    if (!secretFile_.write(payload.data(), static_cast<std::streamsize>(payload.size()))) {
        std::cerr << "ERROR: Failed to write the recovered secret file.\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Decoder::doDecoding() {
    std::cout << "INFO:  ## Decoding Procedure Started ##\n";

    std::cout << "INFO:  Opening required files\n";
    if (openImageFile() != Status::Success) {
        return Status::Failure;
    }
    std::cout << "INFO:  Opened " << stegoImageName_ << '\n';

    std::cout << "INFO:  Skipping BMP header\n";
    if (skipBmpHeader() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Decoding magic string signature\n";
    if (decodeMagicString() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Decoding secret file extension size\n";
    if (decodeSecretFileExtnSize() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Decoding secret file extension\n";
    if (decodeSecretFileExtn() != Status::Success) {
        return Status::Failure;
    }
    std::cout << "INFO:  Output file is " << secretFileName_ << '\n';

    std::cout << "INFO:  Decoding secret file size\n";
    if (decodeSecretFileSize() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Decoding secret file data\n";
    if (decodeSecretFileData() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Done\n";
    return Status::Success;
}

} // namespace stego
