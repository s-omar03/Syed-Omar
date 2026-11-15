cmake_minimum_required(VERSION 3.16)
project(stego LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stego
    src/main.cpp
    src/Encoder.cpp
    src/Decoder.cpp
    src/LsbCodec.cpp
)

target_include_directories(stego PRIVATE include)

if(MSVC)
    target_compile_options(stego PRIVATE /W4)
else()
    target_compile_options(stego PRIVATE -Wall -Wextra -Wpedantic)
endif()
