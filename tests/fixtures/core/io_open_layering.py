import io
import os
import threading


path = "xlang3_io_open_layering.tmp"
with io.open(path, "w+", encoding="utf-8") as stream:
    print(type(stream).__module__, type(stream).__qualname__)
    print(type(stream.buffer).__module__, type(stream.buffer).__qualname__)
    print(stream.buffer is stream, type(stream.buffer.read(0)).__name__)
    stream.write("alpha")

with io.open(path, "rb") as stream:
    print(type(stream).__module__, type(stream).__qualname__, stream.read())

with io.open(path, "rb", buffering=0) as stream:
    print(type(stream).__module__, type(stream).__qualname__, stream.read(2))

descriptor = os.open(path, os.O_RDONLY)
with os.fdopen(descriptor, "r", encoding="utf-8") as stream:
    read = stream.buffer.read
    print(type(stream).__module__, type(stream).__qualname__)
    print(type(stream.buffer).__module__, type(stream.buffer).__qualname__)
    print(type(read(2)).__name__, read(3))

read_descriptor, write_descriptor = os.pipe()
os.write(write_descriptor, b"x")
partial = []


def read_partial_pipe():
    with io.FileIO(read_descriptor, "rb", closefd=False) as stream:
        partial.append(stream.read(8192))


reader = threading.Thread(target=read_partial_pipe)
reader.start()
reader.join(0.5)
print("partial-pipe-read", not reader.is_alive(), partial)
os.close(write_descriptor)
reader.join()
os.close(read_descriptor)

os.remove(path)
