# Plain make build, for when CMake isn't handy.
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude

SRCS := src/main.cpp src/Encoder.cpp src/Decoder.cpp src/LsbCodec.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := stego

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
