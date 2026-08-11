# bffsree - Optimizing Brainfuck Interpreter

A fast, optimizing Brainfuck interpreter written in C. Parses to an
intermediate representation, collapses loops into arithmetic at parse
time, and runs it on a threaded-dispatch VM.

NO JIT. NO COMPILATION. NO ASM.

NOTE: LLM was used to generate the benchmark scripts, make files, and this README.md

Original article: http://sree.kotay.com/2013/02/implementing-brainfuck.html

Esolang benchmarks:  
https://esolangs.org/wiki/User:David.werecat/BFBench  
https://esolangs.org/wiki/Brainfuck_speed_test  


## How fast?

Seconds, best of 7 interleaved runs, Linux x86-64 (gcc 13, same box, same day):

| | mandelbrot | long | hanoi | factor | golden | **total** |
|---|---|---|---|---|---|---|
| **bffsree** (default) | 1.18 | 0.06 | 0.013 | 0.35 | 0.013 | **1.61** |
| **bffsree** (`make fast`) | 1.14 | 0.06 | 0.009 | 0.31 | 0.013 | **1.54** |
| [bf-cpp](https://github.com/jumbub/bf-cpp) | 1.31 | 0.35 | 0.070 | 0.29 | 0.014 | 2.04 |
| [tritium](https://github.com/rdebath/Brainfuck) `-r` (interpreter) | 1.94 | 0.05 | 0.023 | 0.44 | 0.018 | 2.48 |
| tritium JIT (reference, not an interpreter) | 0.43 | 0.006 | 0.015 | 0.07 | 0.009 | 0.54 |

Mandelbrot is a dead heat with bf-cpp (within run-to-run noise); the
overall margin comes from loop collapse (`long`, `hanoi` run ~6x
faster). The JIT row is the compile-to-native ceiling, included for
scale — bffsree still beats it on `hanoi`.

## Features

- **Optimizing IR**:
  - Run-length encoding for consecutive `+`, `-`, `<`, `>`
  - Loop collapse (`[-]` → zero, `[->+<]` → multiply-add, chained copies → `MUL_MUL`)
  - Scan loops (`[>]`, `[<<]`) → single strided scan op
  - Walking loops with arithmetic bodies → single-op internal loops
  - Pointer movement fused into every op (`off` field)
- **Threaded dispatch**: computed-goto on GCC/Clang, switch elsewhere (`-DBF_USE_CGOTO=0/1`)
- **Bounds-safe by default**: every access checked; the tape also carries
  permanently-zero sentinel pads so the `fast` build can skip per-op checks
  without leaving the allocation
- **Built-in profiler**: dynamic op histogram and hottest loop sites (`make prof`)
- **Configurable cell size**: 8, 16, 32, or 64-bit, signed or unsigned
- **Single-header style**: easy to embed
- **Cross-platform**: Linux, macOS, Windows

## Building

Requires a C compiler and Make (`cl /O2 /W3 /Fe:bffsree.exe main.c` works too).

```bash
make            # default: O3, fully bounds-checked
make fast       # benchmark build: unchecked dispatch (sentinel-pad safe), -march=native
make prof       # profiling build: op histogram + hottest loops on stderr
make ref        # reference interpreter (no optimization), for comparison
make debug      # -O0 -g
make clean
```

Cell size and signedness are compile-time options:

```bash
make CELL_BITS=16       # 16-bit unsigned cells (default: 8-bit unsigned)
make CELL_SIGNED=1      # signed cells
```

## Usage

```bash
./bffsree program.b        # run
./bffsree -m program.b     # show optimization metrics
./bffsree -c program.b     # dump optimized IR (readable)
./bffsree -j program.b     # dump optimized IR (JSON)
```

### Profiling

`make prof` builds an interpreter that prints a dynamic profile to
stderr after each run: a histogram of executed IR ops and the hottest
loop sites with their IR bodies.

```bash
make prof
./bffsree program.b > /dev/null
# //-- profile: 449757846 op executions (loop ops count iterations)
# //   MZSCAN         156377434   34.8%
# //   ...
# //-- hottest loop sites: ...
```

### Input

1. **Standard input**: type after the program starts
2. **Embedded**: `!` in the source separates program from input

```brainfuck
,+.!A
```
Reads 'A', increments, outputs 'B'.

## Benchmarks

Standard benchmark programs live in `BFBench-1.4/`
(mandelbrot, factor, long, hanoi, golden, beer, bench).

```bash
./run_benchmarks.sh        # Linux/macOS (-b to force rebuild)
python run_benchmarks.py   # any platform
```

## Optimizations

**Run-length encoding** — consecutive ops merge:
```
+++++  →  VAL +5
```

**Loop collapse** — loops that run `ptr[0]` times flatten to arithmetic:
```brainfuck
[-]        →  VAL_ZERO
[->+<]     →  VAL_MZ    (add to neighbor, zero counter)
[->+++<]   →  VAL_MZ    (multiply by 3)
[->+>+<<]  →  VAL_MUL + VAL_MZ
```
Collapsed output is legal analyzer input, so nested loops collapse
recursively; copies of copies become `MUL_MUL`.

**Scan loops** — `[>]`, `[<<]` etc. become a single strided `PTR_S`.

**Walking loops** — loops with net pointer drift can't flatten, but
one-op bodies run as a single op (`MZSCAN`, `VALSCAN`) and straight-line
arithmetic bodies run without re-entering dispatch (`LOOPRUN`).

**Offset fusion** — trailing pointer movement folds into each op's
`off` field, so `++>+>` is two ops, not four.

## IR Opcodes

| Opcode | Description |
|--------|-------------|
| `NOOP` | Pointer move only |
| `VAL` | Add immediate to current cell |
| `PUT` / `GET` | Output / input current cell |
| `FWD` / `REW` | Loop start / end (conditional jumps) |
| `PTR_S` | Strided scan for zero cell |
| `MUL_MUL` | Chained multiply-accumulate |
| `VAL_MZ` | Multiply-accumulate, zero the counter |
| `VAL_MUL` | Multiply-accumulate |
| `VAL_ZERO` | Set cell to constant |
| `MZSCAN` / `VALSCAN` | Walking loop with one-op body |
| `LOOPRUN` | Walking loop with arithmetic body, run internally |
| `EOP` | End of program |

## Project Structure

```
bffsree/
├── main.c               # Entry point, IR printer, profile report
├── bffsree.h            # Types, config knobs, VM API, op list
├── bffsree.c            # Interpreter/evaluator
├── bffsree-opt.c        # Optimizer (BF text → IR)
├── Makefile
├── run_benchmarks.sh    # Benchmark runner (bash)
├── run_benchmarks.py    # Benchmark runner (Python, cross-platform)
└── BFBench-1.4/         # Benchmark programs
```

## Embedding

Single-header style; can run cooperatively (a limited number of
instructions per call).

```c
#define BFFSREE_IMPLEMENTATION
#define BFFSREE_OPT_IMPLEMENTATION

#include "bffsree.h"
#include "bffsree.c"
#include "bffsree-opt.c"

int main() {
    bf_VM vm;
    bf_VM_alloc(&vm);
    bf_VM_tape(&vm, 65536);

    // Load program into vm.prog, vm.progLen
    // ...

    vm.progLen_op = bf_Optimize(&vm.prog_op, vm.prog, vm.progLen, 0);

    do {
        bffsree_Eval(&vm, NULL, 10000);
    } while (vm.pc > 0);

    bf_VM_free(&vm);
    return 0;
}
```

## License

Public domain / MIT - use as you wish.
