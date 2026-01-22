# bffsree - Optimizing Brainfuck Interpreter

A fast, optimizing Brainfuck interpreter written in C. Features compile-time optimizations including run-length encoding, loop strength reduction, and multiply-accumulate pattern recognition.

NOTE: LLM was used to generate the benchmark scripts, make files, and this README.md

Original article: http://sree.kotay.com/2013/02/implementing-brainfuck.html

Esolang benchmarks:  
https://esolangs.org/wiki/User:David.werecat/BFBench  
https://esolangs.org/wiki/Brainfuck_speed_test  


```
==============================================
         bffsree Benchmark Suite
==============================================

Running benchmarks...
----------------------------------------------
Test                           Time  Status
----------------------------------------------
Mandelbrot                  1.241s  [PASS]
Factoring                   0.337s  [PASS]
Long Run                    0.092s  [PASS]
Golden Ratio                0.031s  [PASS]
Hanoi                       0.038s  [PASS]
99 Bottles of Beer          0.027s  [PASS]
Simple Benchmark            0.021s  [PASS]
----------------------------------------------
Benchmarks complete!
```
ALL TESTS RUN ON M3 2024 MACBOOK

## Features

- **Optimizing IR**: Compiles Brainfuck to an intermediate representation with:
  - Run-length encoding for consecutive `+`, `-`, `<`, `>`
  - Loop strength reduction (`[-]` → zero, `[->+<]` → multiply-add)
  - Scan optimization (`[>]` → pointer scan)
  - Combined multiply-zero operations
- **Configurable cell size**: 8, 16, or 32-bit cells (signed or unsigned)
- **Single-header design**: Easy to embed in other projects
- **Cross-platform**: Works on Linux, macOS, and Windows

## Building

### Prerequisites

- C compiler (GCC, Clang, or MSVC)
- Make (GNU Make, or nmake on Windows)

### Linux / macOS

```bash
# Standard build (O3 optimization)
make

# Debug build (no optimization)
make debug

# Clean
make clean
```

### Windows

**Using MinGW/MSYS2:**
```bash
make
```

**Using Visual Studio Developer Command Prompt:**
```cmd
cl /O2 /W3 /Fe:bfsree.exe main.c
```

### Build Options

Configure cell size and signedness at compile time:

```bash
# 16-bit unsigned cells (default is 8-bit unsigned)
make CELL_BITS=16

# 8-bit signed cells
make CELL_SIGNED=1
```

## Usage

```bash
# Run a Brainfuck program
./bffsree program.b

# Show optimization metrics
./bffsree -m program.b

# Output optimized IR as C-like dump
./bffsree -c program.b

# Output optimized IR as JSON
./bffsree -j program.b
```

### Input Handling

Programs can receive input in two ways:
1. **Standard input**: Type input after the program starts
2. **Embedded input**: Use `!` in the source file to separate program from input

```brainfuck
,+.!A
```
This reads 'A', increments it, and outputs 'B'.

## Benchmarks

The `BFBench-1.4/` directory contains standard Brainfuck benchmark programs.

### Running Benchmarks

**Linux / macOS:**
```bash
./run_benchmarks.sh
```

**Windows (PowerShell):**
```powershell
python run_benchmarks.py
```

**Force rebuild before benchmarking:**
```bash
./run_benchmarks.sh -b
```

### Benchmark Programs

| Program | Description |
|---------|-------------|
| `mandelbrot.b` | Renders ASCII Mandelbrot fractal |
| `factor.b` | Prime factorization |
| `long.b` | Long-running stress test |
| `hanoi.b` | Towers of Hanoi solver |
| `golden.b` | Calculates golden ratio digits |
| `beer.b` | 99 Bottles of Beer lyrics |
| `bench.b` | Simple benchmark |

## Optimizations Explained

### 1. Run-Length Encoding
Consecutive operations are merged:
```
+++++  →  VAL +5
>>>>   →  PTR +4
```

### 2. Loop Strength Reduction
Simple loops are converted to direct operations:
```brainfuck
[-]        →  VAL_ZERO (set cell to 0)
[->+<]     →  VAL_MUL (multiply-add to adjacent cell, zero current)
[->+++<]   →  VAL_MZ (multiply by 3, add to adjacent, zero current)
```

### 3. Scan Optimization
Pointer scan loops are optimized:
```brainfuck
[>]   →  PTR_S (scan right for zero)
[<]   →  PTR_S (scan left for zero)
```

### 4. Fused Operations
Operations are fused with pointer movement:
```brainfuck
++>+>  →  VAL +1, off=1; VAL +1, off=1
```

## IR Opcodes

| Opcode | Description |
|--------|-------------|
| `NOOP` | No operation (with optional pointer offset) |
| `VAL` | Add value to current cell |
| `PUT` | Output current cell |
| `GET` | Input to current cell |
| `FWD` | Forward jump (loop start) |
| `REW` | Reverse jump (loop end) |
| `PTR_S` | Scan for zero cell |
| `VAL_MUL` | Multiply-accumulate |
| `VAL_MZ` | Multiply-accumulate and zero |
| `VAL_ZERO` | Set cell to value (usually 0) |
| `MUL_MUL` | Multiply-multiply (nested loops) |
| `EOP` | End of program |

## Project Structure

```
bffsree/
├── main.c           # Entry point
├── bfsree.h         # Header with types and VM API
├── bfsree.c         # Interpreter/evaluator
├── bfsree-opt.c     # Optimizer
├── Makefile         # Build configuration
├── run_benchmarks.sh    # Benchmark runner (bash)
├── run_benchmarks.py    # Benchmark runner (Python, cross-platform)
└── BFBench-1.4/     # Benchmark programs
```

## Embedding

bfsree uses a single-header style. Note you can run it cooperatively (e.g. a limited number of instructions)  

To embed in your project:

```c
#define BFFSREE_IMPLEMENTATION
#define BFFSREE_OPT_IMPLEMENTATION

#include "bfsree.h"
#include "bfsree.c"
#include "bfsree-opt.c"

int main() {
    bf_VM vm;
    bf_VM_alloc(&vm);
    bf_VM_tape(&vm, 65536);
    
    // Load program into vm.prog, vm.progLen
    // ...
    
    vm.progLen_op = bf_Optimize(&vm.prog_op, vm.prog, vm.progLen, 0);
    
    do {
        bfsree_Eval(&vm, NULL, 10000);
    } while (vm.pc > 0);
    
    bf_VM_free(&vm);
    return 0;
}
```

## License

Public domain / MIT - use as you wish.
