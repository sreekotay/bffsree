# =====================================================================
# Makefile for bfsree - Optimizing Brainfuck Interpreter
# Cross-platform: Linux, macOS, Windows (MinGW/MSYS2)
# =====================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2
LDFLAGS  =

# Detect Windows
ifeq ($(OS),Windows_NT)
    TARGET   = bfsree.exe
    RM       = del /f /q 2>nul || true
    PATHSEP  = \\
else
    TARGET   = bfsree
    RM       = rm -f
    PATHSEP  = /
endif

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

# Reference interpreter (non-optimized IR, for comparison)
ref: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -D_refInterp=1 -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
ref: $(TARGET)

# Clean build artifacts
clean:
ifeq ($(OS),Windows_NT)
	-del /f /q bfsree.exe 2>nul
	-del /f /q *.o 2>nul
else
	rm -f bfsree bfsree.exe *.o
endif

# Run with a test file
test: $(TARGET)
ifeq ($(OS),Windows_NT)
	@if exist BFBench-1.4$(PATHSEP)hanoi.b ( \
		echo Running hanoi.b... && \
		$(TARGET) BFBench-1.4$(PATHSEP)hanoi.b \
	) else ( \
		echo No test file found \
	)
else
	@if [ -f BFBench-1.4/hanoi.b ]; then \
		echo "Running hanoi.b..."; \
		./$(TARGET) BFBench-1.4/hanoi.b; \
	else \
		echo "No test file found"; \
	fi
endif

# Show optimization metrics
metrics: $(TARGET)
ifeq ($(OS),Windows_NT)
	@if exist BFBench-1.4$(PATHSEP)mandelbrot.b $(TARGET) -m BFBench-1.4$(PATHSEP)mandelbrot.b
else
	@if [ -f BFBench-1.4/mandelbrot.b ]; then \
		./$(TARGET) -m BFBench-1.4/mandelbrot.b; \
	fi
endif

# Run benchmarks (cross-platform via Python)
bench: $(TARGET)
	python3 run_benchmarks.py

.PHONY: all debug release ref clean test metrics bench
