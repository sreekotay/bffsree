local function u8(value)
    return value & 0xFF
end

local function smul(a, b)
    local sign = 0
    local ma = a
    if a >= 128 then
        ma = u8(-a)
        sign = sign + 1
    end
    local mb = b
    if b >= 128 then
        mb = u8(-b)
        sign = sign + 1
    end
    local ah, al = ma // 16, ma % 16
    local bh, bl = mb // 16, mb % 16
    local result = u8(ah * bh * 16 + ah * bl + al * bh + al * bl // 16)
    return sign == 1 and u8(-result) or result
end

local function mandel(cr, ci)
    local zr, zi, iterations = 0, 0, 0
    while true do
        local azr = zr >= 128 and u8(-zr) or zr
        local azi = zi >= 128 and u8(-zi) or zi
        if azr > 32 or azi > 32 then
            return iterations
        end
        local zr2, zi2, zri = smul(zr, zr), smul(zi, zi), smul(zr, zi)
        zr, zi = u8(zr2 - zi2 + cr), u8(zri + zri + ci)
        iterations = iterations + 1
        if iterations >= 20 then
            return iterations
        end
    end
end

local ci = 236
while ci ~= 21 do
    local line = ""
    local cr = 216
    while cr ~= 25 do
        local n = mandel(cr, ci)
        local pixel = n >= 20 and "#" or n >= 12 and "%" or
            n >= 8 and "+" or n >= 5 and ":" or n >= 3 and "." or " "
        line = line .. pixel
        cr = u8(cr + 1)
    end
    print(line)
    ci = u8(ci + 1)
end
