local function check_tree(depth, value)
    if depth == 0 then
        return value
    end
    return value
        + check_tree(depth - 1, value * 2)
        + check_tree(depth - 1, value * 2 + 1)
end

local checksum = 0
for _ = 1, 100 do
    checksum = (checksum + check_tree(7, 1)) & 0xFFFFFFFF
end
print(checksum)
