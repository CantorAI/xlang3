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

# Views retain a zero-copy storage lease, including slices and derived views.
mapped = mmap.mmap(-1, 8)
mapped.write(b"abcdefgh")
view = memoryview(mapped)
assert bytes(view) == b"abcdefgh" and not view.readonly
assert view.obj is mapped
view[0] = ord("Z")
mapped.seek(0)
assert mapped.read(1) == b"Z"
child = view[1:4]
derived = memoryview(child)
view.release()
child.release()
assert bytes(derived) == b"bcd"
try:
    mapped.close()
    raise AssertionError("close accepted live export")
except BufferError:
    assert not mapped.closed
derived.release()
derived.release()
mapped.seek(0)
assert mapped.write(mapped) == 8
mapped.seek(0)
assert mapped.read() == b"Zbcdefgh"
mapped.close()
assert mapped.closed

# Dropping the mmap object cannot unmap storage used by a surviving view.
mapped = mmap.mmap(-1, 4)
mapped.write(b"live")
view = memoryview(mapped)
del mapped
assert bytes(view) == b"live"
view.release()

with tempfile.TemporaryFile() as file:
    file.write(b"readonly")
    file.flush()
    mapped = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
    view = memoryview(mapped)
    assert view.readonly and bytes(view) == b"readonly"
    try:
        view[0] = 0
        raise AssertionError("readonly view accepted write")
    except TypeError:
        pass
    # mmap.write acquires a native SDK buffer directly from the mapping.
    with mmap.mmap(-1, 8) as destination:
        assert destination.write(mapped) == 8
        destination.seek(0)
        assert destination.read() == b"readonly"
    view.release()
    mapped.close()
print("mmap buffers ok")
