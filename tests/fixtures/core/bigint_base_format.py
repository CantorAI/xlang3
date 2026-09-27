for value in (0, 2**63, 2**128 + 37, -(2**130 + 255)):
    print(bin(value))
    print(oct(value))
    print(hex(value))

value = 2**80 + 0x1234ABCD
print(f"{value:x}")
print(format(value, "X"))
print(format(value, "#x"))
print(format(value, "_x"))
print(format(value, "+d"))
print(format(-value, "030x"))
