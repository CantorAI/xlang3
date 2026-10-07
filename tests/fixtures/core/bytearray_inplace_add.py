def append_bytes(initial, addition):
    value = bytearray(initial)
    original = value
    value += addition
    return bytes(value), value is original


print("bytes-iadd", append_bytes(b"a", b"bc"))
print("bytearray-iadd", append_bytes(b"a", bytearray(b"bc")))
print("memoryview-iadd", append_bytes(b"a", memoryview(b"bc")))

value = bytearray(b"x")
original = value
value += value
print("self-iadd", bytes(value), value is original)

value = bytearray(b"a")
view = memoryview(value)
try:
    value += b"b"
except BufferError:
    print("exported-iadd", bytes(value))

value += b""
print("empty-exported-iadd", bytes(value))
