import io
import _io


s = io.StringIO("ab")
print(s.read(1))
print(s.tell())
print(s.write("Z"))
s.seek(0)
print(s.read())
print(s.getvalue())
s.close()

b = _io.BytesIO(b"xy")
print(b.read(1))
print(b.write(b"Z"))
b.seek(0)
print(b.read())
for stream in (s, b):
    try:
        stream.detach()
    except Exception as exc:
        print(type(exc).__name__)
try:
    b.seek(0.0)
except Exception as exc:
    print(type(exc).__name__)
print(io.DEFAULT_BUFFER_SIZE)
print(io.TextIOBase)
print(io.TextIOWrapper.__name__, _io.TextIOWrapper.__name__, issubclass(io.TextIOWrapper, io.TextIOBase))
print(io.FileIO.__name__, issubclass(io.FileIO, io.RawIOBase), issubclass(io.BufferedReader, io.BufferedIOBase))
raw = _io.BytesIO()
text = _io.TextIOWrapper(raw, encoding="utf-8")
detached = text.detach()
detached.write(b"ok")
detached.seek(0)
print(detached.read(), text.closed)
try:
    text.read()
except Exception as exc:
    print(type(exc).__name__)
