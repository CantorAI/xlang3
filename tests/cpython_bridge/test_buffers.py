import sys
import gc
sys.path.insert(0, sys.argv[1])
import xlang3
module = xlang3.importModule("buffer_fixture", fromPath=sys.argv[2])
for size in [0, 1, 257, 65536, 1048576]:
    data = bytearray(size)
    shared = xlang3.buffer(data)
    view = memoryview(shared)
    assert view.nbytes == size
    assert not view.readonly
    if size:
        view[0] = 17
        assert data[0] == 17
        module.mutate(shared)
        assert data[0] == (89 if size == 1 else 90)
    try:
        data.append(0)
    except BufferError:
        pass
    else:
        raise AssertionError("CPython buffer owner was not pinned")
    view.release()
    del view, shared
    gc.collect()
    data.append(0)

original = module.get_data()
view = memoryview(original)
assert bytes(view) == b"abcd"
view[0] = 65
assert original[0] == 65
assert module.check_pinned()
module.same_size()
assert bytes(view) == b"Axyd"
view.release()
module.resize()
assert len(original) == 5
parent = module.get_view()
view = memoryview(parent)
parent.release()
assert bytes(view) == b"Axyd!"
view.release()
typed = memoryview(module.cast_view())
assert typed.format == "I" and typed.itemsize == 4 and typed[0] == 1
typed[0] = 19
assert typed[0] == 19
typed.release()
readonly = memoryview(xlang3.buffer(b"abc"))
assert readonly.readonly and bytes(readonly) == b"abc"
try:
    xlang3.buffer(memoryview(bytearray(10))[::2])
except BufferError:
    pass
else:
    raise AssertionError("noncontiguous storage must not be silently copied")
print("Shared buffers: bidirectional writes, pinned storage, typed views, release, size boundaries PASS")
