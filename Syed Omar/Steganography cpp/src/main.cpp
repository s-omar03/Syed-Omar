/*
 * Steganography — LSB encoder/decoder for 24-bit BMP images.
 *
 * Originally written in C by Nestin Gregorios Sunny (30.07.2025),
 * ported to C++20.
 *
 * Usage:
 *   ./stego -e <.bmp file> <.txt|.c|.sh file> [output .bmp]
 *   ./stego -d <.bmp file> [output name without extension]
 */

#include <iostream>
#include <string>
#include <vector>

#include "stego/Decoder.hpp"
#include "stego/Encoder.hpp"
#include "stego/Types.hpp"

namespace {

stego::OperationType checkOperationType(const std::string& flag) {
    if (flag == "-e") {
        return stego::OperationType::Encode;
    }
    if (flag == "-d") {
        return stego::OperationType::Decode;
    }
    return stego::OperationType::Unsupported;
}

void printUsage(const std::string& program) {
    std::cout << program << ": Encoding: " << program
              << " -e <.bmp file> <.txt file> [output file]\n"
              << program << ": Decoding: " << program
              << " -d <.bmp file> [output file]\n";
}

int runEncode(const std::vector<std::string>& args) {
    std::cout << "***** Encoding *****\n";

    stego::Encoder encoder;
    if (encoder.readAndValidateArgs(args) != stego::Status::Success) {
        std::cerr << "ERROR: Read and validate encode arguments failed.\n";
        return 1;
    }
    std::cout << "INFO:  Read and validate encode arguments done\n";

    if (encoder.doEncoding() != stego::Status::Success) {
        std::cerr << "ERROR: Encoding failed.\n";
        return 1;
    }

    std::cout << "INFO:  ## Encoding Done Successfully ##\n";
    std::cout << "INFO:  Wrote " << encoder.stegoImageName() << '\n';
    return 0;
}

int runDecode(const std::vector<std::string>& args) {
    std::cout << "***** Decoding *****\n";

    stego::Decoder decoder;
    if (decoder.readAndValidateArgs(args) != stego::Status::Success) {
        std::cerr << "ERROR: Read and validate decode arguments failed.\n";
        return 1;
    }
    std::cout << "INFO:  Read and validate decode arguments done\n";

    if (decoder.doDecoding() != stego::Status::Success) {
        std::cerr << "ERROR: Decoding failed.\n";
        return 1;
    }

    std::cout << "INFO:  ## Decoding Done Successfully ##\n";
    std::cout << "INFO:  Wrote " << decoder.secretFileName() << '\n';
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string> args(argv, argv + argc);

    if (args.size() < 2) {
        printUsage(args[0]);
        return 1;
    }

    int result = 1;
    switch (checkOperationType(args[1])) {
        case stego::OperationType::Encode:
            result = runEncode(args);
            break;
        case stego::OperationType::Decode:
            result = runDecode(args);
            break;
        case stego::OperationType::Unsupported:
            std::cerr << "ERROR: Unsupported operation type.\n";
            printUsage(args[0]);
            return 1;
    }

    if (result == 0) {
        std::cout << "***** Project Success *****\n";
    }
    return result;
}
