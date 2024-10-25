# Makefile for compiling sh.c

# Compiler and flags
CC = gcc
CFLAGS = -Werror

# Target executable
TARGET = sh
SRC = sh.c

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean target
clean:
	rm -f $(TARGET)

.PHONY: all clean
