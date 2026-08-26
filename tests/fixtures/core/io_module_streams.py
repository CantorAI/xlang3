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
print(io.DEFAULT_BUFFER_SIZE)
print(io.TextIOBase)
print(io.TextIOWrapper.__name__, _io.TextIOWrapper.__name__, issubclass(io.TextIOWrapper, io.TextIOBase))
print(io.FileIO.__name__, issubclass(io.FileIO, io.RawIOBase), issubclass(io.BufferedReader, io.BufferedIOBase))
