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

The article's exact `fib(38)` was tested as generated Brainfuck, but did
not complete in 60 seconds on the development machine. `fib(26)` takes
about five seconds under bffsree-fast and keeps an 11-run comparison
practical. Binary-tree parameters are reduced for the same reason.
Every language uses the reduced parameters in this suite.

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

The original Plush `fib` and `binary_tree` sources are from commit
`e83f5515`; the matched ports intentionally change only the documented
parameters and binary-tree representation.

[article]: https://pointersgonewild.com/2026-09-02-plushs-new-register-based-interpreter/
[go2bf]: https://github.com/itchyny/go2bf
