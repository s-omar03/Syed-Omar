# Steganography (C++20)

LSB steganography for 24-bit BMP images: hides a `.txt`, `.c` or `.sh` file inside
the pixel data of a bitmap and recovers it later. C++20 port of the original C
project by Nestin Gregorios Sunny (Emertxe, 30.07.2025).

## Build

With CMake:

```sh
cmake -B build
cmake --build build
./build/stego
```

Or with plain make:

```sh
make
./stego
```

Both use `-Wall -Wextra -Wpedantic` and build warning-free on GCC 13.

## Usage

```sh
# hide secret.txt inside beautiful.bmp -> stego.bmp
./stego -e beautiful.bmp secret.txt stego.bmp

# output image is optional; defaults to stego.bmp
./stego -e beautiful.bmp secret.txt

# recover it -> recovered.txt (extension comes from the image)
./stego -d stego.bmp recovered

# output name is optional; defaults to decoded_output.<ext>
./stego -d stego.bmp
```

Give the decoder an output **stem** without an extension: the original extension
is stored in the image and appended for you.

## Layout on disk

```
include/stego/     Types.hpp  LsbCodec.hpp  Encoder.hpp  Decoder.hpp
src/               main.cpp   LsbCodec.cpp  Encoder.cpp  Decoder.cpp
CMakeLists.txt     Makefile
```

## Data format

Everything after the 54-byte BMP header, one bit per image byte, MSB first:

| Field              | Image bytes consumed |
| ------------------ | -------------------- |
| Magic string `#*`  | 16                   |
| Extension length   | 32                   |
| Extension          | 8 × length           |
| Secret file size   | 32                   |
| Secret file bytes  | 8 × size             |
| Remainder          | copied verbatim      |

## What changed in the port

Structural:

- `EncodeInfo` / `DecodeInfo` structs plus free functions became `Encoder` and
  `Decoder` classes with private state and private steps.
- Bit packing moved into a stateless `LsbCodec` so it can be tested apart from
  file I/O.
- `FILE*` and `fopen`/`fread` became `std::ifstream` / `std::ofstream`, so files
  close themselves and there is no leak on an early error return.
- `malloc`/`strcpy`/`strcat` for filenames became `std::string`; extension
  parsing uses `std::filesystem::path::extension()` instead of `strchr`.
- Fixed-size `char` buffers became `std::array`, passed as `std::span`.
- `Status` and `OperationType` are now scoped enums, so `Status::Success` cannot
  silently convert to an `int`.
- Return values are `[[nodiscard]]`, which is what catches mistake 1 below at
  compile time.

Bugs fixed along the way:

1. `do_decoding` wrote `e_failure;` as a bare statement in two places instead of
   `return e_failure;`, so a failed open or header skip kept going.
2. `decode_int_from_lsb` looped `i < 31`, dropping the last bit — every decoded
   length was wrong whenever bit 0 mattered.
3. The decoder hardcoded `strcat(secret_fname, ".txt")`, ignoring the extension
   it had just decoded, so hiding a `.c` or `.sh` file came back as `.txt`.
4. That same `strcat` wrote past a buffer allocated for exactly `"nestin"`.
5. Files were opened in text mode (`"r"` / `"w"`), which corrupts binary pixel
   data on Windows. All streams now use `std::ios::binary`.
6. `check_capacity` computed the requirement with a stray extra `+ 4` and a
   hardcoded extension length, so the estimate did not match what was written.
7. `decode_magic_string` compared against a buffer that was never guaranteed to
   be filled; short reads are now detected.
8. No bound was placed on decoded lengths, so a non-stego image could make the
   decoder try to allocate gigabytes. Sizes are now sanity-checked.
9. `encode_secret_file_data` used a variable-length array sized from the file,
   risking a stack overflow on a large secret. It now uses a heap `std::string`.

Behaviour kept deliberately: the `#*` magic string, the 54-byte header, the
MSB-first bit order, and the same `-e` / `-d` argument shapes. Images produced by
the C version decode with this one and vice versa, aside from the bit-31 bug in
point 2.
