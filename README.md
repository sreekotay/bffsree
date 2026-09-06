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

Median seconds over 7 interleaved, output-validated runs, Linux x86-64
(bffsree/bf-cpp: GCC 13; Tritium: Clang 18):

| | BFBench mandelbrot | long | hanoi | factor | golden | fib | binary tree | go2bf mandelbrot | **total** |
|---|---|---|---|---|---|---|---|---|---|
| **bffsree** (default) | 0.743 | 0.053 | 0.010 | 0.319 | 0.010 | 3.020 | 1.548 | 3.906 | **9.609** |
| **bffsree** (`make fast`) | 0.733 | 0.055 | 0.008 | 0.285 | 0.009 | 3.705 | 1.876 | 3.845 | **10.516** |
| [bf-cpp](https://github.com/jumbub/bf-cpp) | 0.956 | 0.431 | 0.091 | 0.275 | 0.010 | 4.705 | 2.615 | 4.523 | 13.605 |
| [tritium](https://github.com/rdebath/Brainfuck) `-r` (interpreter) | 1.728 | 0.053 | 0.019 | 0.415 | 0.013 | 7.329 | >30 | >30 | — |
| tritium JIT (reference, not an interpreter) | 0.359 | 0.006 | 0.012 | 0.064 | 0.007 | 3.834 | 22.352 | >30 | — |

BFBench Mandelbrot is now about 1.3x faster than the previous portable
word-scan build after specializing the 9-cell `MZSCAN` slides and the
4-op copy-walk `LOOPRUN`. That is still far from the 3x / Tritium-JIT
band. The extra code in the dispatch function helps that workload and
the default build's total, but it hurts `make fast` on generated Fib
and tree (i-cache / layout). Loop collapse still accounts for the
`long` / `hanoi` margin versus bf-cpp. The JIT row is the compile-to-native
ceiling. `>30` marks a validation timeout; totals are omitted for rows
with a timeout.

## Features

- **Optimizing IR**:
  - Run-length encoding for consecutive `+`, `-`, `<`, `>`
  - Loop collapse (`[-]` → zero, `[->+<]` → multiply-add, chained copies → `MUL_MUL`)
  - Scan loops (`[>]`, `[<<]`) → single strided scan op
  - Specialized 9-cell walking copies for BFBench-style tape frames
  - Walking loops whose bodies are arithmetic, scans, pointer scans, or
    nested walking loops → one `LOOPRUN` (interpreted in C, not dispatch)
  - Portable 64-bit acceleration for stride-3 scans in generated BF
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

### Comparing Brainfuck runtimes

`compare_bf_runtimes.py` runs the same Brainfuck corpus under checked,
fast word64, and reference bffsree builds, bf-cpp, and Tritium's
interpreter and JIT. The corpus combines BFBench 1.4 with generated
`fib`, `binary_tree`, and `mandelbrot` programs. Every timed result is
checked against canonical output.

The default protocol uses seven interleaved runs and median process wall
time. A fixed seed makes the order reproducible. One untimed validation
run per runtime and workload is included; pairs exceeding the timeout
are reported rather than silently omitted.

```bash
make compare                 # skips unavailable external runtimes

# Clone pinned bf-cpp and Tritium revisions, then benchmark every runtime
python3 compare_bf_runtimes.py --prepare --require-all --json results.json

# Unit tests plus a short bffsree correctness comparison
make test-compare
```

Set `BF_CPP` or `TRITIUM` to override external executable commands.
The harness builds all bffsree variants under the ignored
`.bench-build/` directory. See
[`comparison/README.md`](comparison/README.md) for workload parameters,
source revisions, and methodology.

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
With 8-bit cells, stride `+3` and `-3` scans test three candidates per
portable 64-bit `memcpy` load using exact zero-byte detection. Wider
cells retain the scalar implementation.

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
