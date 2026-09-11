import io
import _io
import os


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
configured = _io.TextIOWrapper(_io.BytesIO(), encoding="ascii")
print(configured.reconfigure(encoding="utf-8", errors="replace", line_buffering=True) is None)
print(configured.encoding, configured.errors, configured.line_buffering)
try:
    configured.reconfigure(unknown=True)
except Exception as exc:
    print(type(exc).__name__)
buffered = _io.BytesIO()
wrapper = _io.TextIOWrapper(buffered)
print(wrapper.buffer is buffered)
class NamedBuffer:
    name = "native-buffer"
print(_io.TextIOWrapper(NamedBuffer()).name)
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

path = "xlang3_fileio_native.tmp"
file = _io.FileIO(path, mode="w+")
print(file.mode, file.writable(), file.readable(), isinstance(file, _io.FileIO))
print(file.write(b"abc"))
file.seek(0)
print(file.read())
file.close()
print(file.closed)
file = _io.FileIO(path, "r")
print(file.mode, file.read())
file.close()
os.unlink(path)

reader = _io.BufferedReader(_io.BytesIO(b"abc"))
print(reader.read1(1))
target = bytearray(2)
print(reader.readinto(target), bytes(target))
print(reader.readinto1(target))
