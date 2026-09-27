import mmap
import tempfile


with mmap.mmap(-1, 8) as memory:
    print(len(memory), memory.closed)
    print(memory.write(b"abc"))
    memory.seek(0)
    print(memory.read(4), memory.tell())
print(memory.closed)

with tempfile.TemporaryFile() as file:
    file.write(b"abcdef")
    file.flush()
    with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_WRITE) as memory:
        print(len(memory), memory.read(3))
        memory.seek(0)
        print(memory.write(b"XYZ"))
        memory.flush()
    file.seek(0)
    print(file.read())
    with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_COPY) as memory:
        memory.write(b"123")
        memory.flush()
        memory.seek(0)
        print(memory.read(6))
    file.seek(0)
    print(file.read())
    with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as memory:
        try:
            memory.write(b"no")
        except TypeError as exc:
            print(type(exc).__name__)
