CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -g
TARGET = text_editor

SOURCES = text_editor.c text_ops.c curser_navigations.c file_ops.c display_search.c redo_undo.c
HEADERS = text_editor.h text_ops.h curser_navigations.h file_ops.h display_search.h redo_undo.h

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o
