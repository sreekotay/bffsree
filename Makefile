# =====================================================================
# Makefile for bffsree - Optimizing Brainfuck Interpreter
# Cross-platform: Linux, macOS, Windows (MinGW/MSYS2)
# =====================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O3 -DNDEBUG
LDFLAGS  =

# Detect Windows
ifeq ($(OS),Windows_NT)
    TARGET   = bffsree.exe
    RM       = del /f /q 2>nul || true
    PATHSEP  = \\
else
    TARGET   = bffsree
    RM       = rm -f
    PATHSEP  = /
endif

# Source files
SRCS     = main.c
HEADERS  = bffsree.h bffsree.c bffsree-opt.c

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

# Fast build: drops the per-dispatch bounds check (tape sentinel pads
# keep every access inside the allocation; safety parity with bf-cpp,
# which never checks) and uses native codegen. Benchmark build; the
# default stays fully checked. If you want to chase the last few
# percent, standard gcc PGO (-fprofile-generate / -fprofile-use) on
# top of these flags adds ~0-5% depending on code layout luck.
fast: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -DBF_FAST -march=native -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
fast: $(TARGET)

# Indexed-scan build: samples the common stride-3/8 scans emitted by go2bf,
# then maintains compact zero-cell bitsets only when scans are long enough
# to repay the update overhead.
indexed: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -DBF_FAST -DBF_ZERO_INDEX=1 -march=native -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
indexed: $(TARGET)

# Profiling build: dumps a dynamic op histogram and the hottest loop
# sites to stderr after the run
prof: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -DBF_PROFILE=1 -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
prof: $(TARGET)

# Reference interpreter (non-optimized IR, for comparison)
ref: CFLAGS = -Wall -Wextra -O3 -DNDEBUG -D_refInterp=1 -DBF_CELL_BITS=$(CELL_BITS) -DBF_CELL_SIGNED=$(CELL_SIGNED) -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
ref: $(TARGET)

# Clean build artifacts
clean:
ifeq ($(OS),Windows_NT)
	-del /f /q bffsree.exe 2>nul
	-del /f /q *.o 2>nul
else
	rm -f bffsree bffsree.exe *.o
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

# Compare matched programs across available language runtimes and bffsree.
compare:
	python3 compare_languages.py

test-compare:
	python3 -m unittest -v test_compare_languages.py
	python3 compare_languages.py -n 1 --warmups 0 --runtimes python bffsree

.PHONY: all debug release ref prof fast indexed cell16 cell32 clean test metrics bench compare test-compare

# 16-bit cell build
cell16: CFLAGS = -Wall -Wextra -O3 -DBF_CELL_BITS=16 -DBF_CELL_SIGNED=0 -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
cell16: $(TARGET)

# 32-bit cell build  
cell32: CFLAGS = -Wall -Wextra -O3 -DBF_CELL_BITS=32 -DBF_CELL_SIGNED=0 -DBF_OP_BUF_BITS=$(OP_BUF_BITS)
cell32: $(TARGET)
