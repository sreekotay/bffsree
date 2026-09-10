# Brainfuck runtime comparison

This suite compares one Brainfuck corpus across:

- bffsree checked, fast word64, and unoptimized reference builds
- [bf-cpp][bf-cpp]
- [Tritium][tritium] in interpreter (`-r`) and default JIT modes

It does not run high-level-language ports. This keeps the comparison on
the same Brainfuck source and cell semantics.

## Workloads

The corpus contains the seven BFBench 1.4 programs in this repository
and these generated programs:

| workload | parameters | expected output |
|---|---|---|---|
| `fib` | naive recursive `fib(26)` | `121393` |
| `binary_tree` | depth 7, 100 traversals | wrapping checksum `3264000` |
| `mandelbrot` | 65x41, 20 iterations, signed 4-bit fixed point | 41-line ASCII image |

The article's exact `fib(38)` was tested with the scalar fast
interpreter, but did not complete in 60 seconds on the development
machine. `fib(26)` takes about five seconds with scalar scans and about
3.0 seconds with portable 64-bit stride-3 scans. Binary-tree parameters
are reduced for the same reason.

Brainfuck has no functions, objects, or allocator. `fib.b` uses an
explicit generated call stack and preserves the naive recursive
algorithm. `binary_tree.b` preserves the recursive traversal and node
values, but represents the perfect tree implicitly as `(depth, value)`;
it does **not** measure allocation or object-field pointer chasing like
the article's original benchmark. Results must not be presented as an
apples-to-apples VM object-model comparison.

Mandelbrot uses wrapping 8-bit fixed-point arithmetic. This is distinct
from Plush's interactive 640x480 floating-point Mandelbrot example.

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

The Go files are compiler inputs, not benchmark runtimes. Generated
Brainfuck uses only the standard eight instructions and 8-bit wrapping
cells. The repository is public domain/MIT; go2bf is MIT licensed.
BFBench 1.4 was already vendored by the project and retains its original
attribution in `BFBench.py`.

## Measurement

```bash
python3 compare_bf_runtimes.py
python3 compare_bf_runtimes.py --prepare --require-all --json results.json
```

`--prepare` clones and builds bf-cpp commit
`a7c99b1c98d56534c77edb7c1fe47aae9975d00e` and Tritium commit
`525d346c006ea30dfc847ae3b32ed44d44fa9925` below `.bench-build/`.
Set `BF_CPP` or `TRITIUM` to use existing binaries instead.

Each complete process is timed, including startup and output. Jobs are
shuffled with a fixed seed for every round. Output is validated before
measurement and after every timed run. Raw samples, build commands,
source revisions, skipped timeout pairs, and host information can be
written to JSON. The default timeout is 30 seconds per process, so the
reference interpreter is expected to time out on demanding programs.

## Portable 64-bit scan results

The 8-bit interpreter loads eight bytes with `memcpy`, applies
an exact zero-byte bit hack, and tests the stride-3 candidates at byte
offsets 0, 3, and 6. Five interleaved GCC `-O3` runs measured:

| workload | scalar median | word64 median | speedup |
|---|---:|---:|---:|
| `fib` | 4.418 s | 2.994 s | 1.48x |
| `binary_tree` | 2.321 s | 1.561 s | 1.49x |
| `mandelbrot` | 4.694 s | 4.077 s | 1.15x |

Known little- and big-endian targets use register masks directly.
Unknown byte orders retain a standards-safe object-byte fallback.

An explicit eight-byte shift/OR construction was also tested. Against
the constant-size `memcpy` load, five interleaved runs were 1.79x slower
on `fib`, 1.84x slower on `binary_tree`, and 1.39x slower on
`mandelbrot`. GCC did not combine that source pattern into an equivalent
unaligned word load, so the experiment was removed.

[article]: https://pointersgonewild.com/2026-09-02-plushs-new-register-based-interpreter/
[go2bf]: https://github.com/itchyny/go2bf
[bf-cpp]: https://github.com/jumbub/bf-cpp
[tritium]: https://github.com/rdebath/Brainfuck
