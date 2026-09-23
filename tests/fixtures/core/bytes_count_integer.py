from enum import IntEnum


class Code(IntEnum):
    ONE = 1


for data in (b"\x01\x00\x01", bytearray(b"\x01\x00\x01")):
    for value in (1, True, Code.ONE, 256, -1, "x", 1.0):
        try:
            print(type(data).__name__, repr(value), data.count(value))
        except Exception as error:
            print(type(data).__name__, repr(value), type(error).__name__, str(error))
    print(type(data).__name__, "slice", data.count(1, 1, 3))
