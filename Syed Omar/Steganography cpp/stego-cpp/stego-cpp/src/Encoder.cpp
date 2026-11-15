#include "stego/Encoder.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "stego/LsbCodec.hpp"

namespace stego {
namespace {

/// Returns the file extension including the dot, or an empty string.
std::string extensionOf(const std::string& path) {
    return std::filesystem::path(path).extension().string();
}

/// Pixel-data capacity of a 24-bit BMP: width * height * 3.
/// Width lives at offset 18 and height at offset 22, both 4-byte integers.
std::uint32_t bmpPixelCapacity(std::istream& image) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    image.clear();
    image.seekg(18, std::ios::beg);
    image.read(reinterpret_cast<char*>(&width), sizeof(width));
    image.read(reinterpret_cast<char*>(&height), sizeof(height));

    if (!image) {
        return 0;
    }
    return width * height * 3U;
}

constexpr std::array<std::string_view, 3> kAllowedSecretExtensions{".txt", ".c", ".sh"};

} // namespace

Status Encoder::readAndValidateArgs(const std::vector<std::string>& args) {
    // args[0] = program, args[1] = "-e", args[2] = source image,
    // args[3] = secret file, args[4] = optional stego image.
    if (args.size() < 4) {
        std::cerr << "ERROR: Encoding needs a source .bmp and a secret file.\n";
        return Status::Failure;
    }

    if (extensionOf(args[2]) != ".bmp") {
        std::cerr << "ERROR: Source image file must have a .bmp extension.\n";
        return Status::Failure;
    }
    srcImageName_ = args[2];

    secretExtension_ = extensionOf(args[3]);
    const bool allowed = std::ranges::find(kAllowedSecretExtensions, secretExtension_)
                         != kAllowedSecretExtensions.end();
    if (!allowed) {
        std::cerr << "ERROR: Secret file must be .c, .sh, or .txt.\n";
        return Status::Failure;
    }
    secretFileName_ = args[3];

    if (args.size() < 5) {
        std::cout << "INFO:  Output file not mentioned. Creating "
                  << kDefaultStegoImage << " as default.\n";
        stegoImageName_ = kDefaultStegoImage;
    } else {
        if (extensionOf(args[4]) != ".bmp") {
            std::cerr << "ERROR: Stego image file must have a .bmp extension.\n";
            return Status::Failure;
        }
        stegoImageName_ = args[4];
    }

    return Status::Success;
}

Status Encoder::openFiles() {
    srcImage_.open(srcImageName_, std::ios::binary);
    if (!srcImage_) {
        std::cerr << "ERROR: Unable to open file " << srcImageName_ << '\n';
        return Status::Failure;
    }
    std::cout << "INFO:  Opened " << srcImageName_ << '\n';

    secretFile_.open(secretFileName_, std::ios::binary);
    if (!secretFile_) {
        std::cerr << "ERROR: Unable to open file " << secretFileName_ << '\n';
        return Status::Failure;
    }
    std::cout << "INFO:  Opened " << secretFileName_ << '\n';

    stegoImage_.open(stegoImageName_, std::ios::binary | std::ios::trunc);
    if (!stegoImage_) {
        std::cerr << "ERROR: Unable to open file " << stegoImageName_ << '\n';
        return Status::Failure;
    }
    std::cout << "INFO:  Opened " << stegoImageName_ << '\n';

    return Status::Success;
}

Status Encoder::checkCapacity() {
    imageCapacity_ = bmpPixelCapacity(srcImage_);
    if (imageCapacity_ == 0) {
        std::cerr << "ERROR: Could not read BMP dimensions.\n";
        return Status::Failure;
    }

    secretFile_.seekg(0, std::ios::end);
    const auto size = secretFile_.tellg();
    if (size <= 0) {
        std::cerr << "ERROR: Secret file is empty or unreadable.\n";
        return Status::Failure;
    }
    secretFileSize_ = static_cast<std::uint32_t>(size);
    secretFile_.seekg(0, std::ios::beg);

    const std::size_t required =
        kBmpHeaderSize
        + kBitsPerByte * kMagicString.size()
        + kBitsPerInt                                   // extension length
        + kBitsPerByte * secretExtension_.size()
        + kBitsPerInt                                   // secret file size
        + kBitsPerByte * secretFileSize_;

    std::cout << "INFO:  Checking " << srcImageName_ << " capacity to hold "
              << secretFileName_ << '\n';

    if (imageCapacity_ < required) {
        std::cerr << "ERROR: Image doesn't have enough capacity for encoding.\n";
        return Status::Failure;
    }

    return Status::Success;
}

Status Encoder::copyBmpHeader() {
    std::array<char, kBmpHeaderSize> header{};

    srcImage_.clear();
    srcImage_.seekg(0, std::ios::beg);
    if (!srcImage_.read(header.data(), header.size())) {
        std::cerr << "ERROR: Couldn't read BMP header.\n";
        return Status::Failure;
    }
    if (!stegoImage_.write(header.data(), header.size())) {
        std::cerr << "ERROR: Couldn't write BMP header to stego image.\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Encoder::encodeDataToImage(std::string_view data) {
    std::array<char, kBitsPerByte> buffer{};
    for (const char ch : data) {
        if (!srcImage_.read(buffer.data(), buffer.size())) {
            std::cerr << "ERROR: Ran out of source image data while encoding.\n";
            return Status::Failure;
        }
        LsbCodec::encodeByte(static_cast<std::uint8_t>(ch), buffer);
        if (!stegoImage_.write(buffer.data(), buffer.size())) {
            std::cerr << "ERROR: Failed writing to stego image.\n";
            return Status::Failure;
        }
    }
    return Status::Success;
}

Status Encoder::encodeIntToImage(std::uint32_t value) {
    std::array<char, kBitsPerInt> buffer{};
    if (!srcImage_.read(buffer.data(), buffer.size())) {
        std::cerr << "ERROR: Ran out of source image data while encoding.\n";
        return Status::Failure;
    }
    LsbCodec::encodeInt(value, buffer);
    if (!stegoImage_.write(buffer.data(), buffer.size())) {
        std::cerr << "ERROR: Failed writing to stego image.\n";
        return Status::Failure;
    }
    return Status::Success;
}

Status Encoder::encodeMagicString() {
    return encodeDataToImage(kMagicString);
}

Status Encoder::encodeSecretFileExtnSize() {
    return encodeIntToImage(static_cast<std::uint32_t>(secretExtension_.size()));
}

Status Encoder::encodeSecretFileExtn() {
    return encodeDataToImage(secretExtension_);
}

Status Encoder::encodeSecretFileSize() {
    return encodeIntToImage(secretFileSize_);
}

Status Encoder::encodeSecretFileData() {
    secretFile_.clear();
    secretFile_.seekg(0, std::ios::beg);

    std::string payload(secretFileSize_, '\0');
    if (!secretFile_.read(payload.data(), static_cast<std::streamsize>(payload.size()))) {
        std::cerr << "ERROR: Failed to read secret file contents.\n";
        return Status::Failure;
    }
    return encodeDataToImage(payload);
}

Status Encoder::copyRemainingImageData() {
    stegoImage_ << srcImage_.rdbuf();
    return stegoImage_ ? Status::Success : Status::Failure;
}

Status Encoder::doEncoding() {
    std::cout << "INFO:  Opening required files\n";
    if (openFiles() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  ## Encoding Procedure Started ##\n";

    if (checkCapacity() != Status::Success) {
        return Status::Failure;
    }
    std::cout << "INFO:  Done. Found OK\n";

    std::cout << "INFO:  Copying image header\n";
    if (copyBmpHeader() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Encoding magic string signature\n";
    if (encodeMagicString() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Encoding secret file extension size\n";
    if (encodeSecretFileExtnSize() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Encoding secret file extension\n";
    if (encodeSecretFileExtn() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Encoding " << secretFileName_ << " file size\n";
    if (encodeSecretFileSize() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Encoding " << secretFileName_ << " file data\n";
    if (encodeSecretFileData() != Status::Success) {
        return Status::Failure;
    }

    std::cout << "INFO:  Copying left over data\n";
    if (copyRemainingImageData() != Status::Success) {
        std::cerr << "ERROR: Failed to copy remaining image data.\n";
        return Status::Failure;
    }

    std::cout << "INFO:  Done\n";
    return Status::Success;
}

} // namespace stego
