def u8(value):
    return value & 0xFF


def smul(a, b):
    sign = 0
    ma = a
    if a >= 128:
        ma = u8(-a)
        sign += 1
    mb = b
    if b >= 128:
        mb = u8(-b)
        sign += 1
    ah, al = ma // 16, ma % 16
    bh, bl = mb // 16, mb % 16
    result = u8(ah * bh * 16 + ah * bl + al * bh + al * bl // 16)
    return u8(-result) if sign == 1 else result


def mandel(cr, ci):
    zr = zi = iterations = 0
    while True:
        azr = u8(-zr) if zr >= 128 else zr
        azi = u8(-zi) if zi >= 128 else zi
        if azr > 32 or azi > 32:
            return iterations
        zr2, zi2, zri = smul(zr, zr), smul(zi, zi), smul(zr, zi)
        zr, zi = u8(zr2 - zi2 + cr), u8(zri + zri + ci)
        iterations += 1
        if iterations >= 20:
            return iterations


ci = 236
while ci != 21:
    line = ""
    cr = 216
    while cr != 25:
        n = mandel(cr, ci)
        line += "#" if n >= 20 else "%" if n >= 12 else "+" if n >= 8 else ":" if n >= 5 else "." if n >= 3 else " "
        cr = u8(cr + 1)
    print(line)
    ci = u8(ci + 1)
