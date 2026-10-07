import builtins


assert bytes.decode(b"hello") == "hello"
assert bytes.decode(b"\xc3\xa9", encoding="utf-8") == "é"
assert bytes.decode(b"\xffx", errors="ignore") == "x"
assert bytes.decode(b"\xe9", "latin-1", errors="strict") == "é"
assert bytearray.decode(bytearray(b"hello")) == "hello"
assert bytearray.decode(bytearray(b"\xffx"), errors="ignore") == "x"
print("class decode and keywords", True)


class BytesChild(bytes):
    def decode(self, *args, **kwargs):
        return "python override"


child = BytesChild(b"hello")
assert child.decode() == "python override"
assert bytes.decode(child) == "hello"
print("bytes subclasses and override dispatch", True)


class Pretend:
    def __bytes__(self):
        return b"hello"


pretend = Pretend()
pretend.__xlang3_bytes_value__ = b"hello"
FakeBytes = type("bytes", (), {})
fake = FakeBytes()
fake.__xlang3_bytes_value__ = b"hello"
for invalid in ["text", bytearray(b"hello"), memoryview(b"hello"), pretend, fake, 42]:
    try:
        bytes.decode(invalid)
    except TypeError:
        pass
    else:
        raise AssertionError("invalid bytes descriptor receiver accepted")
try:
    bytearray.decode(b"hello")
except TypeError:
    pass
else:
    raise AssertionError("invalid bytearray descriptor receiver accepted")
print("canonical receiver checks", True)

for invoke in [lambda: bytes.decode(),
               lambda: bytes.decode(b"hello", "utf-8", encoding="ascii"),
               lambda: bytes.decode(b"hello", "utf-8", "strict", errors="ignore"),
               lambda: bytes.decode(b"hello", unexpected=True),
               lambda: bytes.decode(b"hello", 42)]:
    try:
        invoke()
    except TypeError:
        pass
    else:
        raise AssertionError("invalid decoder arguments accepted")
try:
    bytes.decode(b"\xff")
except UnicodeDecodeError:
    pass
else:
    raise AssertionError("decoder exception swallowed")
print("decoder argument and Unicode errors", True)

saved = bytes.decode
saved_array = bytearray.decode
original_bytes, original_bytearray = builtins.bytes, builtins.bytearray
try:
    builtins.bytes = Pretend
    builtins.bytearray = Pretend
    assert saved(b"hello") == "hello"
    assert saved(child) == "hello"
    assert saved_array(original_bytearray(b"hello")) == "hello"
finally:
    builtins.bytes, builtins.bytearray = original_bytes, original_bytearray
print("saved descriptors preserve owners", True)
