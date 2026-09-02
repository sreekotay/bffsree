def check_tree(depth, value):
    if depth == 0:
        return value
    return (
        value
        + check_tree(depth - 1, value * 2)
        + check_tree(depth - 1, value * 2 + 1)
    )


checksum = 0
for _ in range(100):
    checksum = (checksum + check_tree(7, 1)) & 0xFFFFFFFF
print(checksum)
