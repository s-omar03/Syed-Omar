CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = inverted

SRCS = main.c \
       create_database.c \
       display_database.c \
       file_validation.c \
       insert_last.c \
       save_database.c \
       search.c \
       update_database.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c inverted_Search.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET) file1.txt file2.txt file3.txt file4.txt

.PHONY: all clean run