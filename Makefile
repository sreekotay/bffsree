# =====================================================================
# Makefile for bfsree - Optimizing Brainfuck Interpreter
# =====================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2
LDFLAGS  =

# Target executable
TARGET   = bfsree

# Source files
SRCS     = main.c
HEADERS  = bfsree.h bfsree.c bfsree-opt.c

# Build configuration options (override on command line)
# Example: make CELL_BITS=16 CELL_SIGNED=1
CELL_BITS   ?= 8
CELL_SIGNED ?= 0
OP_BUF_BITS ?= 16

CFLAGS += -DBF_CELL_BITS=$(CELL_BITS)
CFLAGS += -DBF_CELL_SIGNED=$(CELL_SIGNED)
CFLAGS += -DBF_OP_BUF_BITS=$(OP_BUF_BITS)

# Default target
all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)

# Debug build with symbols and no optimization
debug: CFLAGS = -Wall -Wextra -g -O0 -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
debug: $(TARGET)

# Release build with maximum optimization
release: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
release: $(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TARGET).exe *.o

# Run with a test file
test: $(TARGET)
	@if [ -f BFBench-1.4/hanoi.b ]; then \
		echo "Running hanoi.b..."; \
		./$(TARGET) BFBench-1.4/hanoi.b; \
	else \
		echo "No test file found"; \
	fi

# Show optimization metrics
metrics: $(TARGET)
	@if [ -f BFBench-1.4/mandelbrot.b ]; then \
		./$(TARGET) -m BFBench-1.4/mandelbrot.b; \
	fi

# Benchmark helper
bench: $(TARGET)
	@echo "Copying $(TARGET) to BFBench-1.4/"
	cp $(TARGET) BFBench-1.4/

.PHONY: all debug release clean test metrics bench
