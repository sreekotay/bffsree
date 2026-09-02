package main

// Source for the generated fib.b Brainfuck program.
// Requires itchyny/go2bf (pinned in comparison/README.md).
func fib(n byte) uint32 {
	if n < 2 {
		return uint32(n)
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	// The article uses fib(38). The all-runtime comparison uses 26 because
	// the exact recursive BF lowering does not complete fib(38) in 60 seconds.
	println(fib(26))
}
