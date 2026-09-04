def u8(value)
  value & 0xFF
end

def smul(a, b)
  sign = 0
  ma = a
  if a >= 128
    ma = u8(-a)
    sign += 1
  end
  mb = b
  if b >= 128
    mb = u8(-b)
    sign += 1
  end
  ah, al = ma.divmod(16)
  bh, bl = mb.divmod(16)
  result = u8(ah * bh * 16 + ah * bl + al * bh + al * bl / 16)
  sign == 1 ? u8(-result) : result
end

def mandel(cr, ci)
  zr = zi = iterations = 0
  loop do
    azr = zr >= 128 ? u8(-zr) : zr
    azi = zi >= 128 ? u8(-zi) : zi
    return iterations if azr > 32 || azi > 32

    zr2, zi2, zri = smul(zr, zr), smul(zi, zi), smul(zr, zi)
    zr, zi = u8(zr2 - zi2 + cr), u8(zri + zri + ci)
    iterations += 1
    return iterations if iterations >= 20
  end
end

ci = 236
while ci != 21
  line = +""
  cr = 216
  while cr != 25
    n = mandel(cr, ci)
    line << if n >= 20
              "#"
            elsif n >= 12
              "%"
            elsif n >= 8
              "+"
            elsif n >= 5
              ":"
            elsif n >= 3
              "."
            else
              " "
            end
    cr = u8(cr + 1)
  end
  puts line
  ci = u8(ci + 1)
end
