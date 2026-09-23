print(b"".isascii())
print(b"abc\x7f".isascii())
print(b"\x80".isascii())
print(bytearray(b"ASCII").isascii())
print(bytearray(b"\xff").isascii())

try:
    b"abc".isascii(1)
except TypeError as exc:
    print(type(exc).__name__)
