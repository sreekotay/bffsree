def check_tree(depth, value)
  return value if depth == 0

  value +
    check_tree(depth - 1, value * 2) +
    check_tree(depth - 1, value * 2 + 1)
end

checksum = 0
100.times do
  checksum = (checksum + check_tree(7, 1)) & 0xFFFFFFFF
end
puts checksum
