CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

TARGET  := mp3_tag_reader
SRCS    := main.c edit.c view.c
OBJS    := $(SRCS:.c=.o)
DEPS    := edit.h view.h type.h

.PHONY: all clean rebuild run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) temp.mp3

rebuild: clean all

# Example: make run ARGS="-v song.mp3"
run: $(TARGET)
	./$(TARGET) $(ARGS)