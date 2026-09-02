package main

// Source for the generated binary_tree.b Brainfuck program.
//
// Brainfuck has no object model or allocator. This preserves the article
// benchmark's recursive traversal shape and node values, representing the
// perfect tree implicitly by (depth, value). The uint32 checksum wraps in the
// same way on every standard 8-bit-cell Brainfuck implementation.
func checkTree(depth byte, value uint32) uint32 {
	if depth == 0 {
		return value
	}
	return value +
		checkTree(depth-1, value*2) +
		checkTree(depth-1, value*2+1)
}

func main() {
	sum := uint32(0)
	// The article uses depth 14 and 2000 traversals. The all-runtime
	// comparison is scaled to keep the generated BF program practical.
	for i := uint16(0); i < 100; i++ {
		sum += checkTree(7, 1)
	}
	println(sum)
}
