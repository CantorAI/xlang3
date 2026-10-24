"""Native array/buffer correctness triggers; no timings or library replacement."""
import array
import json


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


records = []
for name, check in (
    ("scalar_index_coercion", check_scalars),
    ("slice_independent_storage", check_slice_copy),
    ("slice_assignment_overlap_resize", check_slice_assignment),
    ("memoryview_element_metadata", check_view_metadata),
    ("memoryview_two_way_write", check_view_write_through),
    ("exported_resize_transaction", check_export_resize),
    ("derived_view_release_order", check_derived_release),
    ("tolist", check_tolist),
):
    try:
        check()
        records.append({"check": name, "ok": True})
    except Exception as error:
        records.append({"check": name, "ok": False,
                        "error": type(error).__name__, "detail": str(error)})
print(json.dumps({"diagnostic_only": True, "timing": False,
                  "records": records}, indent=2))
