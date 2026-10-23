# Running DOOM on bffsree

[DoomBF](https://github.com/HighGhlow/DoomBF) is DOOM compiled to RISC-V
and then to Brainfuck. The program ships as `doom.bpk`, a 353 MB
run-length-compressed Brainfuck file that expands to about 3.7 trillion
commands. bffsree loads it in its packed form and runs it with no JIT.

On a 4-core x86-64 cloud VM it takes about 6 s to load and optimize,
shows the first frame after about 2.5 minutes, and then renders one
frame every 22–26 s. Frames are byte-identical to DoomBF's own `ibf`
interpreter.

## 1. Get the program

```bash
curl -LO https://fs.highghlow.ru/public/doombf/doom.bpk    # 353 MB
```

The `.bpk` format is plain Brainfuck plus repeat counts: `+{3}` means
`+++` (the count is hex), and every other byte is a comment. bffsree
switches to this format for any file ending in `.bpk`, or with `-p`.

## 2. Build bffsree

```bash
make
```

The default build handles DoomBF at full speed: 32-bit pointer deltas
and loop-move targets, so a byte copied between cells a million apart
becomes a single op. `.bpk` input gets a 512M-cell tape by default
(DoomBF needs about 270M); override it with `-t`, e.g. `-t 1G`.

Memory: about 650 MB resident (430 MB of optimized program plus the
touched part of the tape). `make narrow` builds still run it, but the
long moves stay unoptimized loops and DOOM stalls in startup.

## 3a. Play it with the DoomBF frontend (X11)

`frnt` opens a window, draws each frame, and sends your keypresses
back. The two programs talk in both directions, so connect them with a
named pipe:

```bash
sudo apt-get install libx11-dev            # frnt needs Xlib
git clone https://github.com/HighGhlow/DoomBF
make -C DoomBF/frontend frnt

mkfifo .pipe
./bffsree doom.bpk < .pipe | DoomBF/frontend/frnt > .pipe
```

Keys: arrows move, Enter selects, Ctrl fires, Space opens doors,
Esc opens the menu, Y confirms, Q quits the frontend. The game only
reads keys once per frame, so hold a key until the next frame arrives.

## 3b. Run it headless and save frames

Without a display, feed key events from a file and pull frames out of
the output. Each frame, the program prints `[OP] Getting events` and
reads key bytes until a `0`:

| byte | key | | byte | key |
|---|---|---|---|---|
| `01` | up | | `06` | space |
| `02` | down | | `07` | ctrl |
| `03` | left | | `08` | esc |
| `04` | right | | `09` | y |
| `05` | enter | | `f0` + key | release |

Input must never reach end of file, or the program spins reading EOF;
append `/dev/zero` so that "no more keys" reads as empty frames.

This presses Enter four times (menu, New Game, episode, skill) and then
holds Up to walk forward:

```bash
python3 -c "
import sys
f = [[], []] + [[5], [], [0xf5], [], []] * 4 + [[], [], [1]]
sys.stdout.buffer.write(b''.join(bytes(x) + b'\0' for x in f))
" > events.bin

cat events.bin /dev/zero | ./bffsree doom.bpk > doom.out
```

With no input at all (`< /dev/zero`) it stays on the title screen: the
game clock advances 20 ms per frame, so the attract-mode demo would
start only after about 240 frames.

Each frame in `doom.out` is the line `[OP] Start image output`, one
`0x00` byte, width and height as 16-bit big-endian values (640x480),
then width × height × 3 bytes of RGB. The game draws into the top-left
320x200. To dump the frames as PNGs:

```python
import struct, sys, zlib

data = open(sys.argv[1], "rb").read()
mark, pos, n = b"[OP] Start image output\n", 0, 0
while (i := data.find(mark, pos)) >= 0:
    h = i + len(mark)
    w, ht = data[h + 1] << 8 | data[h + 2], data[h + 3] << 8 | data[h + 4]
    rgb = data[h + 5 : h + 5 + w * ht * 3]
    if len(rgb) < w * ht * 3:
        break
    raw = b"".join(b"\0" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(ht))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(f"frame{n:04d}.png", "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, ht, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    pos, n = h + 5 + w * ht * 3, n + 1
print(n, "frames")
```

## What made it fast

Nearly all of DoomBF's time goes to moving bytes between cells a
megabyte apart, in loops like `[->{100000}+<{100000}]`. bffsree's loop
optimizer used to recognize such move/multiply loops only when every
cell was within 255 of the loop cell, so these ran up to 255 iterations
each, and DOOM stalled for over 15 minutes during texture setup. Far
offsets are now tracked too, and each loop becomes one multiply-add op.
