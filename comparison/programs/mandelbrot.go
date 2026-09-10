package main

// Source for the generated mandelbrot.b Brainfuck program.
// Signed fixed-point multiplication uses scale 16 (16 units = 1.0).
func smul(a, b byte) byte {
	sign := byte(0)
	ma := a
	if a >= 128 {
		ma = 0 - a
		sign++
	}
	mb := b
	if b >= 128 {
		mb = 0 - b
		sign++
	}
	ah, al := ma/16, ma%16
	bh, bl := mb/16, mb%16
	result := ah*bh*16 + ah*bl + al*bh + al*bl/16
	if sign == 1 {
		result = 0 - result
	}
	return result
}

func mandel(cr, ci byte) byte {
	zr, zi, iterations := byte(0), byte(0), byte(0)
	done := byte(0)
	for done == 0 {
		azr := zr
		if zr >= 128 {
			azr = 0 - zr
		}
		azi := zi
		if zi >= 128 {
			azi = 0 - zi
		}
		if azr > 32 || azi > 32 {
			done = 1
		}
		if done == 0 {
			zr2, zi2, zri := smul(zr, zr), smul(zi, zi), smul(zr, zi)
			zr, zi = zr2-zi2+cr, zri+zri+ci
			iterations++
			if iterations >= 20 {
				done = 1
			}
		}
	}
	return iterations
}

func main() {
	for ci := byte(236); ci != 21; ci++ {
		for cr := byte(216); cr != 25; cr++ {
			n := mandel(cr, ci)
			if n >= 20 {
				print("#")
			} else if n >= 12 {
				print("%")
			} else if n >= 8 {
				print("+")
			} else if n >= 5 {
				print(":")
			} else if n >= 3 {
				print(".")
			} else {
				print(" ")
			}
		}
		print("\n")
	}
}
