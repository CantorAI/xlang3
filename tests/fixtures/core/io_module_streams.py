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
print(b.isatty(), _io.StringIO().isatty())
view = b.getbuffer()
view[0] = ord("Z")
print(b.getvalue())
print(b.read(1))
direct_view = _io.BytesIO(b"pq")
direct_buffer = direct_view.getbuffer()
direct_buffer[1] = ord("Q")
print(direct_view.read())
direct_buffer.release()
try:
    b.write(b"Z")
except Exception as exc:
    print(type(exc).__name__)
view.release()
print(b.write(b"Z"))
b.seek(0)
print(b.read())
exported = _io.BytesIO(b"export")
exported_view = exported.getbuffer()
for operation in (lambda: exported.truncate(), lambda: exported.close()):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)
exported_view.release()
exported.close()
print(exported.closed)
readinto_source = _io.BytesIO(b"pq")
readinto_target = bytearray(2)
print(readinto_source.readinto1(readinto_target), bytes(readinto_target))
for stream in (s, b):
    try:
        stream.detach()
    except Exception as exc:
        print(type(exc).__name__)
b.close()
try:
    b.isatty()
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
file = _io.FileIO(path, "r")
print(file.readall())
file.close()
file = _io.FileIO(path, "r")
target = bytearray(2)
print(file.readinto(target), bytes(target))
file.close()
os.unlink(path)

reader = _io.BufferedReader(_io.BytesIO(b"abc"))
print(reader.peek(2))
print(reader.read1(1))
target = bytearray(2)
print(reader.readinto(target), bytes(target))
print(reader.readinto1(target))
newline_none = _io.StringIO("a\rb\nc\r\nd", newline=None)
print(repr(newline_none.getvalue()), repr(newline_none.newlines))
newline_crlf = _io.StringIO(newline="\r\n")
print(newline_crlf.write("x\ny"), repr(newline_crlf.getvalue()), repr(newline_crlf.newlines))
newline_empty = _io.StringIO(newline="")
newline_empty.write("x\ry\nz\r\n")
print(repr(newline_empty.newlines))
universal_lines = _io.StringIO("one\rtwo\nthree\r\n", newline="")
print([line for line in universal_lines])
carriage_lines = _io.StringIO("one\ntwo\r", newline="\r")
print([line for line in carriage_lines])
text_newlines = _io.TextIOWrapper(_io.BytesIO(b"one\rtwo\nthree\r\n"), newline=None)
print(repr(text_newlines.read()), repr(text_newlines.newlines))
