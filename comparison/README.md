# Plush-style cross-language comparison

This suite adds bffsree to a comparison shaped like the one in
[Plush's New Register-Based Interpreter Is Insanely Fast][article].
It runs matched programs under Plush, Python, Ruby, Lua, and Brainfuck,
using 11 interleaved samples and the median process wall time.

## Workloads

| workload | matched parameters | article parameters | expected output |
|---|---|---|---|
| `fib` | naive recursive `fib(26)` | naive recursive `fib(38)` | `121393` |
| `binary_tree` | depth 7, 100 traversals | depth 14, 2000 traversals | wrapping checksum `3264000` |
| `mandelbrot` | 65x41, 20 iterations, signed 4-bit fixed point | new workload | 41-line ASCII image |

The article's exact `fib(38)` was tested with the scalar fast
interpreter, but did not complete in 60 seconds on the development
machine. `fib(26)` takes about five seconds with scalar scans and about
0.9 seconds with AVX2 stride-3 scans. Binary-tree parameters are reduced
for the same reason. Every language uses the reduced parameters in this
suite.

Brainfuck has no functions, objects, or allocator. `fib.b` uses an
explicit generated call stack and preserves the naive recursive
algorithm. `binary_tree.b` preserves the recursive traversal and node
values, but represents the perfect tree implicitly as `(depth, value)`;
it does **not** measure allocation or object-field pointer chasing like
the article's original benchmark. Results must not be presented as an
apples-to-apples VM object-model comparison.

Mandelbrot uses wrapping 8-bit fixed-point arithmetic in every language,
matching the Brainfuck cell model. This is distinct from Plush's
interactive 640x480 floating-point Mandelbrot example.

## Brainfuck source generation

The `.b` files are generated from their adjacent `.go` source files with
[itchyny/go2bf][go2bf] commit
`6001981a6834e6cc908888d0089bc18947ea16cb` (MIT):

```bash
git clone https://github.com/itchyny/go2bf.git
cd go2bf
git checkout 6001981a6834e6cc908888d0089bc18947ea16cb
go build -o go2bf .

./go2bf /path/to/bffsree/comparison/programs/fib.go \
  > /path/to/bffsree/comparison/programs/fib.b
./go2bf /path/to/bffsree/comparison/programs/binary_tree.go \
  > /path/to/bffsree/comparison/programs/binary_tree.b
./go2bf /path/to/bffsree/comparison/programs/mandelbrot.go \
  > /path/to/bffsree/comparison/programs/mandelbrot.b
```

The Go files are compiler inputs, not additional benchmark runtimes.
Generated Brainfuck uses only the standard eight instructions and
8-bit wrapping cells.

## Measurement

```bash
python3 compare_languages.py
python3 compare_languages.py --require-all --json results.json
```

Each complete interpreter process is timed, including startup and output.
Jobs are shuffled with a fixed seed for every round. Output is validated
before measurement and after every timed run. The report uses Python as
the relative-throughput baseline when Python is present and records all
raw samples, runtime versions, build flags, workload parameters, and
host information in JSON.

Because the scaled workloads finish in milliseconds in the high-level
runtimes, their figures include a significant amount of process-startup
time. The bffsree programs take seconds. These results are useful for
end-to-end execution comparisons, but are not pure VM dispatch rates and
should not be substituted for the article's exact `fib(38)` and
depth-14 tree results.

The original Plush `fib` and `binary_tree` sources are from commit
`e83f5515`; the matched ports intentionally change only the documented
parameters and binary-tree representation.

## SIMD scan results

On the development host (Clang 18, AVX2 x86-64), seven
compiler-matched interleaved runs compared the scalar fast interpreter
with vectorized stride-3 scans:

| workload | scalar median | SIMD median | speedup |
|---|---:|---:|---:|
| `fib` | 4.970 s | 0.921 s | 5.40x |
| `binary_tree` | 2.685 s | 0.610 s | 4.40x |
| `mandelbrot` | 5.292 s | 3.660 s | 1.45x |

The AVX2 implementation checks eleven stride-3 candidate cells per
unaligned 32-byte load. Sentinel padding keeps each load inside the tape
allocation. Builds without AVX2, non-GCC/Clang compilers, and non-8-bit
cells use the unchanged scalar scan.

## Portable 64-bit scan results

`make word` uses an alternative implementation with no SIMD or
`-march=native` requirement. It loads eight bytes with `memcpy`, applies
an exact zero-byte bit hack, and tests the stride-3 candidates at byte
offsets 0, 3, and 6. Five interleaved GCC `-O3` runs measured:

| workload | scalar median | word64 median | speedup |
|---|---:|---:|---:|
| `fib` | 4.418 s | 2.994 s | 1.48x |
| `binary_tree` | 2.321 s | 1.561 s | 1.49x |
| `mandelbrot` | 4.694 s | 4.077 s | 1.15x |

Known little- and big-endian targets use register masks directly.
Unknown byte orders retain a standards-safe object-byte fallback.
AVX2 remains the faster option where available.

An explicit eight-byte shift/OR construction was also tested. Against
the constant-size `memcpy` load, five interleaved runs were 1.79x slower
on `fib`, 1.84x slower on `binary_tree`, and 1.39x slower on
`mandelbrot`. GCC did not combine that source pattern into an equivalent
unaligned word load, so the experiment was removed.

[article]: https://pointersgonewild.com/2026-09-02-plushs-new-register-based-interpreter/
[go2bf]: https://github.com/itchyny/go2bf
