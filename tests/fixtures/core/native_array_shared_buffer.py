"""Native array/buffer correctness triggers; no timings or library replacement."""
import array
import gc
import weakref


def items(value):
    return [value[i] for i in range(len(value))]


def check_scalars():
    class Index:
        def __index__(self):
            return 1
    value = array.array("i", [10, 20, 30])
    value[Index()] = 77
    assert value[Index()] == 77 and value[-1] == 30
    assert items(value) == [10, 77, 30]


def check_slice_copy():
    value = array.array("d", [1.25, 2.5, 3.75, 5.0])
    selected = value[::-2]
    assert selected.typecode == "d" and items(selected) == [5.0, 2.5]
    selected[0] = 99.0
    assert value[-1] == 5.0


def check_slice_assignment():
    value = array.array("i", [0, 1, 2, 3, 4, 5])
    value[::2] = value[1::2]
    assert items(value) == [1, 1, 3, 3, 5, 5]
    value[1:4] = array.array("i", [8])
    assert items(value) == [1, 8, 5, 5]
    value[:] = value
    assert items(value) == [1, 8, 5, 5]


def check_view_metadata():
    value = array.array("d", [1.25, 2.5, 3.75])
    view = memoryview(value)
    assert view.obj is value
    assert view.format == "d" and view.itemsize == value.itemsize
    assert view.shape == (3,) and view.strides == (value.itemsize,)
    assert len(view) == 3 and view.nbytes == 3 * value.itemsize
    view.release()


def check_view_write_through():
    value = array.array("d", [1.25, 2.5, 3.75])
    view = memoryview(value)
    view[0] = 9.0
    assert value[0] == 9.0
    value[1] = 7.0
    assert view[1] == 7.0 and view[0] == 9.0
    raw = view.cast("B")
    raw[0] = 0
    assert view.tobytes() == value.tobytes()
    raw.release()
    view.release()


def expect_buffer_error(value, operation):
    before = value.tobytes()
    try:
        operation()
    except BufferError:
        pass
    else:
        raise AssertionError("exported array was resized")
    assert value.tobytes() == before


def check_export_resize():
    value = array.array("i", [1, 2, 3])
    view = memoryview(value)
    expect_buffer_error(value, lambda: value.append(4))
    expect_buffer_error(value, lambda: value.frombytes(array.array("i", [4]).tobytes()))
    expect_buffer_error(value, lambda: value.__setitem__(slice(1, 2), array.array("i", [7, 8])))
    expect_buffer_error(value, lambda: value.__imul__(2))
    value[0] = 99
    value[:] = array.array("i", [7, 8, 9])
    assert items(view) == [7, 8, 9]
    view.release()
    value.append(4)
    assert items(value) == [7, 8, 9, 4]


def check_derived_release():
    value = array.array("i", [1, 2, 3, 4])
    parent = memoryview(value)
    derived = parent[1:3]
    readonly = derived.toreadonly()
    assert derived.obj is value and readonly.obj is value
    parent.release()
    expect_buffer_error(value, lambda: value.append(5))
    derived.release()
    assert items(readonly) == [2, 3]
    expect_buffer_error(value, lambda: value.append(5))
    readonly.release()
    value.append(5)
    assert items(value) == [1, 2, 3, 4, 5]


def check_tolist():
    value = array.array("i", [1, 2, 3])
    assert value.tolist() == [1, 2, 3]


for check in (check_scalars, check_slice_copy, check_slice_assignment, check_view_metadata,
              check_view_write_through, check_export_resize, check_derived_release, check_tolist):
    check()


# Character-list slicing is an independent oracle for every array slice.
original = list(range(11))
for start in (None, -50, -1, 0, 1, 5, 50, 10**100):
    for stop in (None, -50, -1, 0, 7, 50, -(10**100)):
        for step in (None, 1, 2, -1, -3, 2**63 - 1, -(2**63)):
            key = slice(start, stop, step)
            value = array.array("i", original)
            selected = value[key]
            assert selected.tolist() == original[key], repr((start, stop, step, selected.tolist(), original[key]))
            replacement = array.array("i", [99] * len(original[key]))
            value[key] = replacement
            expected = original[:]
            expected[key] = list(replacement)
            assert value.tolist() == expected


class Index:
    def __index__(self):
        return 2


value = array.array("i", [0, 1, 2, 3, 4])
assert value[slice(Index(), None, Index())].tolist() == [2, 4]
for index in (10**100, -(10**100)):
    try:
        value[index]
    except IndexError:
        pass
    else:
        raise AssertionError("huge array index accepted")
for key, replacement, error_type in (
    (slice(None, None, 0), array.array("i"), ValueError),
    (slice(None, None, 2), array.array("i", [1]), ValueError),
    (slice(None), array.array("d", [1.0]), TypeError),
    (slice(None), [1], TypeError),
):
    before = value.tobytes()
    try:
        value[key] = replacement
    except error_type:
        pass
    else:
        raise AssertionError("invalid array slice assignment accepted")
    assert value.tobytes() == before


class Subclass(array.array):
    pass


subclass = Subclass("i", [1, 2, 3])
assert type(subclass[:]) is array.array and type(subclass * 2) is array.array
subclass.cycle = subclass
reference = weakref.ref(subclass)
del subclass
gc.collect()
assert reference() is None


class FakeMetadata(array.array):
    @property
    def typecode(self):
        return "d"

    @property
    def itemsize(self):
        return 999


fake = FakeMetadata("i", [11, 22])
assert fake.typecode == "d" and fake.itemsize == 999
view = memoryview(fake)
assert view.obj is fake and view.format == "i" and view.itemsize == array.array("i").itemsize
assert view.shape == (2,) and view[1] == 22
view.release()


# No-op operations under an export remain legal; successful writes retain
# the physical storage used by preexisting views.
value = array.array("i", [1, 2, 3])
view = memoryview(value)
value *= 1
value.frombytes(b"")
expect_buffer_error(value, lambda: value.__setitem__(slice(0, 0), array.array("i")))
assert list(view) == [1, 2, 3]
view.release()
def check_stride(step):
    values = array.array("i", [10, 20, 30, 40, 50, 60])
    parent = memoryview(values)
    selected = parent[::step]
    expected = [10, 30, 50] if step == 2 else [60, 40, 20]
    assert selected.obj is values, "strided .obj must retain exporter"
    assert selected.strides == (step * values.itemsize,)
    assert selected.shape == (3,) and selected.nbytes == 3 * values.itemsize
    assert not selected.readonly
    assert list(selected) == expected
    selected[1] = 99
    physical_index = 2 if step == 2 else 3
    assert values[physical_index] == 99
    expected[1] = 99
    assert selected.tobytes() == array.array("i", expected).tobytes()
    duplicate = memoryview(selected)
    parent.release()
    selected.release()
    try:
        values.append(70)
    except BufferError:
        pass
    else:
        raise AssertionError("derived strided export did not prevent resizing")
    assert list(duplicate) == expected and duplicate.obj is values
    duplicate.release()
    values.append(70)
    assert values[-1] == 70


check_stride(2)
check_stride(-2)

value = array.array("i", [10, 20, 30, 40, 50, 60])
parent = memoryview(value)
reverse = parent[::-1]
selected = reverse[1::2]
assert selected.tolist() == [50, 30, 10]
assert selected.strides == (-2 * value.itemsize,)
selected[:] = memoryview(array.array("i", [7, 8, 9]))
assert value.tolist() == [9, 20, 8, 40, 7, 60]
assert bytes(selected) == selected.tobytes() == array.array("i", [7, 8, 9]).tobytes()
assert selected.hex() == bytes(selected).hex()
try:
    selected.cast("B")
except TypeError:
    pass
else:
    raise AssertionError("non-contiguous cast accepted")
# Overlap must snapshot values before changing any source element.
reverse[:] = parent
assert value.tolist() == [60, 7, 40, 8, 20, 9]
empty = selected[0:0]
assert empty.tolist() == [] and empty.tobytes() == b"" and empty.obj is value
for view in (parent, reverse, selected, empty):
    view.release()
value.append(70)
assert value[-1] == 70

readonly = memoryview(b"abcdef")[::2]
assert readonly == b"ace" and b"ace" == readonly
assert hash(readonly) == hash(b"ace")
assert {readonly: "present"}[b"ace"] == "present"
assert bytes(readonly) == readonly.tobytes() == b"ace"
assert int.from_bytes(readonly, "big") == int.from_bytes(b"ace", "big")
assert b"%b" % readonly == b"ace"
import hashlib
import binascii
import marshal
import re
for call, expected_error in (
    (lambda: binascii.hexlify(readonly), BufferError),
    (lambda: hashlib.sha256(readonly), BufferError),
    (lambda: marshal.dumps(readonly), ValueError),
    (lambda: marshal.loads(readonly), BufferError),
    (lambda: re.search(b"a", readonly), TypeError),
):
    try:
        call()
    except expected_error:
        pass
    else:
        raise AssertionError("non-contiguous native buffer consumer accepted invalid storage")
readonly.release()

print("native array shared buffer semantics ok")
