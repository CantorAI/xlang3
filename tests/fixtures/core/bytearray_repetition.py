mask = bytearray(b"\x01\x02\x03\x04")
tripled = mask * 3
print(type(tripled) is bytearray)
print(tripled == bytearray(b"\x01\x02\x03\x04" * 3))
print(2 * mask == bytearray(b"\x01\x02\x03\x04" * 2))
print(mask * 0 == bytearray())
print(mask * -2 == bytearray())
tripled[0] = 9
print(mask[0], tripled[0])
