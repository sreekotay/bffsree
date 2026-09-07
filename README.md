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
| **bffsree** (default) | 0.482 | 0.033 | 0.007 | 0.276 | 0.009 | 2.771 | 1.405 | 4.093 | **9.076** |
| **bffsree** (`make fast`) | 0.483 | 0.036 | 0.008 | 0.277 | 0.009 | 2.773 | 1.418 | 4.050 | **9.054** |
| [bf-cpp](https://github.com/jumbub/bf-cpp) | 0.957 | 0.431 | 0.091 | 0.274 | 0.010 | 4.703 | 2.614 | 4.524 | 13.604 |
| [tritium](https://github.com/rdebath/Brainfuck) `-r` (interpreter) | 1.727 | 0.053 | 0.019 | 0.416 | 0.014 | 7.329 | >30 | >30 | — |
| tritium JIT (reference, not an interpreter) | 0.359 | 0.006 | 0.012 | 0.064 | 0.007 | 3.837 | 22.319 | >30 | — |

BFBench Mandelbrot went from 0.743 to 0.482 in one pass of profile-
driven work with no new dispatch code in Eval: nest templates for its
two hottest scan-carrying loop bodies, the `LOOPRUN` helper variant
decoded once instead of per entry, run-once bodies left inline, and a
four-way stride scan (mandelbrot scans 488M cells at stride 9). Fib and
tree gained from the same `LOOPRUN` and scan changes; `long` from the
run-once rule. The remaining gap to the Tritium JIT is the
compile-to-native ceiling. `>30` marks a validation timeout; totals are
omitted for rows with a timeout.

The compiler no longer decides the result. Same harness, same box,
`CC=clang` (Clang 18, default / `make fast`): mandelbrot 0.514 / 0.508,
long 0.033 / 0.035, hanoi 0.008 / 0.008, factor 0.273 / 0.284, golden
0.010 / 0.010, fib 2.742 / 2.735, tree 1.359 / 1.362, go2bf mandelbrot
4.188 / 4.166, total 9.127 / 9.108 — within 0.6% of GCC overall and
within 7% on any one workload. Before the layout work (see *Code
layout* below) the GCC total was 9.898 against Clang's 9.115, with
`long` 48% and `factor` 13% slower under GCC from identical source.

## Features

- **Optimizing IR**:
  - Run-length encoding for consecutive `+`, `-`, `<`, `>`
  - Loop collapse (`[-]` → zero, `[->+<]` → multiply-add, chained copies → `MUL_MUL`)
  - Scan loops (`[>]`, `[<<]`) → single strided scan op
  - 9-cell tape frames: lane slides to offset 9 and +9 copy/mul walks
    keep the current frame in registers, then hop a whole record
  - Walking loops with arithmetic bodies → single-op internal loops,
    with the helper variant decided once at optimize time
  - Affine reconstruction: a walking `LOOPRUN` body is composed into
    `new[i] = bias + Σ c[i][j]*old[j]` (mod the cell ring). Small trees
    (1–3 stores) are evaluated as straight-line C picked by shape
    (`s1` / `s2z` / `s2` / `s3`): compile once, bind the window, eval
    each hop. `./bffsree -c` prints the maps. Not an AST walk and not
    a coefficient-loop interpreter. Hop and window come from the IR.
    The map also exposes run-once bodies (no drift, loop cell left at
    0), which stay inline in the dispatcher instead of a walker.
  - Nest templates: scan-carrying loops (which `LOOPRUN` cannot take)
    whose body matches a corpus-dominant structural signature run as
    one straight-line helper with all parameters in locals — the
    `LOOPRUN` helpers one level up. `./bffsree -c` prints every loop's
    signature (`ZVRMV`, `VM{VRS}SmMV`, ...)
  - Block clears (`[-]>[-]>[-]`) → one `ZFILL`
  - Portable 64-bit acceleration for stride-3 scans in generated BF;
    other strides scan four cells per iteration
  - Pointer movement fused into every op (`off` field)
- **Threaded dispatch**: computed-goto on GCC/Clang, switch elsewhere (`-DBF_USE_CGOTO=0/1`).
  One indirect jump per opcode is kept under GCC (its crossjumping
  pass is off for the VM file), and every hot function is 64-byte
  aligned so an edit elsewhere cannot move the dispatch loop across a
  fetch window. GCC and Clang builds now land within a few percent of
  each other on every workload.
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
stderr after each run: a histogram of executed IR ops, the hottest
loop sites with their IR bodies, and the loops still dispatched by
Eval ranked by direct dispatches (body ops plus back-edges — where
dispatch time actually goes). `BF_PROF_DUMP=1` adds every op with its
count for offline analysis; `./bffsree -c` prints each loop's
structural signature next to its `FWD`, so a hot signature can be
matched to a nest template.

```bash
make prof
./bffsree program.b > /dev/null
# //-- profile: 449757846 op executions (loop ops count iterations)
# //   MZSCAN         156377434   34.8%
# //   ...
# //-- hottest loop sites: ...
# //-- unconverted loops by direct dispatches (body ops + back-edges): ...
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
run per runtime and workload is included; pairs exceeding the 30-second
timeout are reported rather than silently omitted. Every test and bench
harness uses that same 30s cap.

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
portable 64-bit `memcpy` load using exact zero-byte detection; the
three lanes are masked and tested with one branch per word, and the
stopping lane is resolved after the loop (three in-loop tests cost
GCC ~20% on fib/tree against Clang; one test costs neither). Every
other stride scans four cells per iteration from four independent
loads; the scalar loop is bound by one taken branch per cell, and
mandelbrot scans 488M cells at stride 9 (553 → 480 ms). The scan may
read up to three strides past the zero it finds, which the sentinel
pads absorb.

**Walking loops** — loops with net pointer drift can't flatten, but
one-op bodies run as a single op (`MZSCAN`, `VALSCAN`) and straight-line
arithmetic bodies run without re-entering dispatch (`LOOPRUN`). Which
internal helper a `LOOPRUN` uses (two 4-op copy-walk shapes, the
affine shape evaluators, or the generic walker) is decoded once into
the op, so an entry costs a cell test and a switch.

**Run-once loops** — a body with no pointer drift that leaves the loop
cell at constant 0 is an if-block: `[` skips it when zero, and the `]`
never jumps back. The affine map identifies these; they stay in the
dispatcher rather than paying for an internal walker (that was
`long.b`'s hottest site).

**Nest templates** — loops with no I/O whose body contains scans or
nested loops cannot be `LOOPRUN`s. When the body's structural signature
matches a template, the whole body runs as one straight-line C helper
with every `val`/`off`/`buf` hoisted into locals once per entry; nested
loops are inlined and every loop-carrying piece tests its cell before
calling anything. Templates are picked from a corpus-wide ranking of
loop bodies by direct dispatches (`make prof` with `BF_PROF_DUMP=1`):

| signature | body | where |
|---|---|---|
| `ZVRMV` | `VAL_ZERO VAL` · `LOOPRUN` · `VAL_MZ VAL` | mandelbrot, 11 sites |
| `VM{VRS}SmMV` | `VAL VAL_MZ` · `{VAL LOOPRUN PTR_S}` · `PTR_S MZSCAN` · `VAL_MZ VAL` | mandelbrot, 7 sites |

A *generic* nest runner was built first — the body cut into segments
(affine block bound to a shape evaluator, scan, block helper, child
nest) and run by a small switch — and measured slower than leaving the
loop to the computed-goto dispatcher (mandelbrot 659 → 745 ms).
Interpreting a map descriptor once per iteration costs more than
dispatching the two or three trivial ops it replaces; the shape
evaluators only pay off inside a `LOOPRUN`, where the descriptor loads
amortize over a tight walk. It stays in the tree behind
`-DBF_NEST_GENERIC=1` as the measured baseline; by default loops
without a template are left to the dispatcher.

**Block clears** — `k ≥ 2` adjacent `VAL_ZERO`s storing the same value
(`[-]>[-]>[-]`) fuse into one `ZFILL`; small fills are straight-line
stores.

**Offset fusion** — trailing pointer movement folds into each op's
`off` field, so `++>+>` is two ops, not four.

**Code layout** — two things made GCC builds slower than Clang builds
of the same source, and both were layout, not codegen. GCC's
crossjumping pass merged the 17 identical dispatch tails of the
computed-goto loop into 7 shared indirect jumps (one predictor entry
for many opcodes) and folded the peeled first iteration of the scan
loops back into the loop; `#pragma GCC optimize("no-crossjumping")`
on the VM file restores one jump per opcode (`long` 49 → 34 ms,
`factor` 308 → 276 ms). Separately, with byte-identical `Eval` code,
a 48-byte shift in its start address moved `factor` by 6%; `Eval` and
every out-of-line helper are now `aligned(64)` so a function's speed
depends only on its own code (`-DBF_HOT_ALIGN_BYTES=0` to disable,
`-DBF_KEEP_CROSSJUMPING` to leave the pass on).

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
| `ZFILL` | Set `k` adjacent cells to a constant |
| `MZSCAN` / `VALSCAN` | Walking loop with one-op body |
| `LOOPRUN` | Walking loop with arithmetic body, run internally |
| `NEST` | Loop whose body matches a template, run as one straight-line helper |
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
