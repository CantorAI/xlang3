from enum import IntEnum


class ByteCode(IntEnum):
    FIRST = 1
    LAST = 255


class Integer(int):
    pass


class OverriddenInteger(int):
    def __index__(self):
        raise RuntimeError("int subclass override must not run")


class Index:
    calls = 0

    def __init__(self, value):
        self.value = value

    def __index__(self):
        Index.calls += 1
        return self.value


class BadIndex:
    def __index__(self):
        return "not an integer"


class FailedIndex:
    def __index__(self):
        raise RuntimeError("index-callback")


class InstanceIndex:
    pass


for constructor in (bytes, bytearray):
    name = constructor.__name__
    Index.calls = 0
    try:
        result = constructor([0, 255, True, False, ByteCode.FIRST,
                              Integer(2), OverriddenInteger(3), Index(4)])
        print(name, "items", result == b"\x00\xff\x01\x00\x01\x02\x03\x04",
              Index.calls == 1)
    except Exception as exc:
        print(name, "items-error", type(exc).__name__)

    errors = []
    for value in (-1, 256, -(1 << 100), 1 << 100,
                  Index(-1), Index(256), Index(1 << 100)):
        try:
            constructor([value])
        except Exception as exc:
            errors.append(type(exc).__name__)
        else:
            errors.append("accepted")
    print(name, "range-errors", errors == ["ValueError"] * 7)

    item = InstanceIndex()
    item.__index__ = lambda: 7
    spoofed = InstanceIndex()
    spoofed._value_ = 65
    spoofed.__xlang3_int_value__ = 65
    errors = []
    for value in (1.5, "x", object(), BadIndex(), item, spoofed):
        try:
            constructor([value])
        except Exception as exc:
            errors.append(type(exc).__name__)
        else:
            errors.append("accepted")
    print(name, "type-errors", errors == ["TypeError"] * 6)

    try:
        constructor([FailedIndex()])
    except Exception as exc:
        print(name, "callback", type(exc) is RuntimeError,
              str(exc) == "index-callback")
    else:
        print(name, "callback", False, False)

    try:
        result = constructor(value for value in (Index(65), ByteCode.LAST))
        print(name, "generator", result == b"A\xff")
    except Exception as exc:
        print(name, "generator-error", type(exc).__name__)
