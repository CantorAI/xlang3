raw_big = b"\x01\x02\x03\x04\x05"
raw_little = raw_big[::-1]
for order, raw in (("big", raw_big), ("little", raw_little)):
    value = int.from_bytes(raw, order)
    print(value == 0x0102030405)
    print(value.to_bytes(5, order) == raw)
    print(int.from_bytes(bytearray(raw), order) == value)
    print(int.from_bytes(memoryview(raw), order) == value)

negative_big = b"\x80\x00\x00\x00\x01"
negative_little = negative_big[::-1]
for order, raw in (("big", negative_big), ("little", negative_little)):
    value = int.from_bytes(raw, order, signed=True)
    print(value == -(1 << 39) + 1)
    print(value.to_bytes(5, order, signed=True) == raw)

print(True.to_bytes(1, "big") == b"\x01")
print(False.to_bytes(1, "big") == b"\x00")
encode_bool = True.to_bytes
print(encode_bool(1, "little") == b"\x01")
print(True.bit_length() == 1 and False.bit_length() == 0)
wide = (1 << 80) + 0x0102030405
wide_bytes = wide.to_bytes(11, "big")
print(int.from_bytes(wide_bytes, "big") == wide)
