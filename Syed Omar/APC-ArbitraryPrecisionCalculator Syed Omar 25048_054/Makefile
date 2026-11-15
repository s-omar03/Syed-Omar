CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g
TARGET  = apc
SRCS    = main.c apc.c addition.c subtraction.c multiplication.c division.c
OBJS    = $(SRCS:.c=.o)
DEPS    = apc.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@echo "--- Addition ---"
	./$(TARGET) 999 + 1
	./$(TARGET) -10 + 5
	@echo "--- Subtraction ---"
	./$(TARGET) 1000 - 1
	./$(TARGET) 20 - 50
	@echo "--- Multiplication ---"
	./$(TARGET) 99 '*' 99
	./$(TARGET) -10 '*' -5
	@echo "--- Division ---"
	./$(TARGET) 100 / 5
	./$(TARGET) 10 / 0

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean test
