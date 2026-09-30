def rotate_locals():
    a = 1
    b = 2
    c = 3
    d = 4
    i = 0
    while i < 1000:
        a = b
        b = c
        c = d
        d = i
        i = i + 1
    return a, b, c, d, i


def overflow_counter():
    a = 10
    b = 20
    c = 30
    d = 40
    i = 9223372036854775806
    while i < 9223372036854775807:
        a = b
        b = c
        c = d
        d = i
        i = i + 2
    return a, b, c, d, i


print(rotate_locals())
print(overflow_counter())
