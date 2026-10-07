import builtins
import gzip
import io


class Lines(io.BufferedIOBase):
    def __init__(self, values):
        self.values = list(values)

    def readline(self):
        return self.values.pop(0)


lines = Lines([b"one\n", b"two\n", b""])
assert iter(lines) is lines
assert next(lines) == b"one\n"
assert list(lines) == [b"two\n"]

text = Lines(["é\n", "終\n", ""])
assert list(text) == ["é\n", "終\n"]

# The native base must look up the Python implementation on every next().
lines = Lines([b"original\n", b""])
assert next(lines) == b"original\n"
lines.readline = lambda: b"instance replacement\n"
assert next(lines) == b"instance replacement\n"
del lines.readline
original_readline = Lines.readline
Lines.readline = lambda self: b"class replacement\n"
assert next(lines) == b"class replacement\n"
Lines.readline = original_readline


class Hooked(io.IOBase):
    def __getattribute__(self, name):
        if name == "readline":
            return lambda: b"attribute hook\n"
        return super().__getattribute__(name)


assert next(Hooked()) == b"attribute hook\n"

# Native I/O uses runtime attribute/length primitives, unaffected by Python
# rebinding the public builtins module's getattr and len names.
original_getattr = builtins.getattr
original_len = builtins.len
builtins.getattr = lambda *args: None
builtins.len = lambda *args: 0
lines = Lines([[1, 2], []])
assert next(lines) == [1, 2]
builtins.getattr = original_getattr
builtins.len = original_len
assert next(lines, "end") == "end"
try:
    next(Lines([b""]))
except StopIteration as error:
    assert error.args == ()
else:
    assert False, "empty readline result did not end iteration"


class Closed(io.IOBase):
    @property
    def closed(self):
        return True


try:
    iter(Closed())
except ValueError:
    pass
else:
    assert False, "iteration must honor the virtual closed property"


class ClosedFailure(io.IOBase):
    @property
    def closed(self):
        raise LookupError("closed getter")


try:
    iter(ClosedFailure())
except LookupError as error:
    assert str(error) == "closed getter"
else:
    assert False, "closed property exception was lost"


class ReadFailure(io.IOBase):
    def readline(self):
        raise LookupError("readline failure")


try:
    next(ReadFailure())
except LookupError as error:
    assert str(error) == "readline failure"
else:
    assert False, "readline exception was lost"


class SizedLine:
    def __init__(self, size):
        self.size = size

    def __len__(self):
        return self.size


sized = SizedLine(True)
lines = Lines([sized, SizedLine(False)])
assert next(lines) is sized
assert next(lines, "empty") == "empty"
for invalid_size, exception in ((-1, ValueError), ("invalid", TypeError), (2 ** 100, OverflowError)):
    try:
        next(Lines([SizedLine(invalid_size)]))
    except exception:
        pass
    else:
        assert False, "invalid readline length was accepted"

payload = b"first\nsecond\nlast"
compressed = gzip.compress(payload)
with gzip.GzipFile(fileobj=io.BytesIO(compressed), mode="rb") as stream:
    assert iter(stream) is stream
    assert list(stream) == [b"first\n", b"second\n", b"last"]
    assert next(stream, "eof") == "eof"
try:
    iter(stream)
except ValueError:
    pass
else:
    assert False, "closed GzipFile was iterable"

print("native IOBase iteration ok")
