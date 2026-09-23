import struct

print(b"ab".rjust(5))
print(b"ab".rjust(5, b"0"))
print(b"ab".rjust(1, b"0"))
print(bytearray(b"ab").rjust(4, b"0"))
print(struct.unpack(">Q", b"\x6a\xb3\x81\x8c".rjust(8, b"\x00"))[0])
try:
    b"ab".rjust(4, b"01")
except TypeError as error:
    print(type(error).__name__)
