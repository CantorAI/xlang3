"""Strided array memoryview ownership and mutation checks; no timings."""
from array import array
import json


def check_stride(step):
    values = array("i", [10, 20, 30, 40, 50, 60])
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
    assert selected.tobytes() == array("i", expected).tobytes()
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


records = []
for step in (2, -2):
    try:
        check_stride(step)
        records.append({"step": step, "ok": True})
    except Exception as error:
        records.append({"step": step, "ok": False,
                        "error": type(error).__name__, "detail": str(error)})
print(json.dumps({"diagnostic_only": True, "timing": False,
                  "records": records}, indent=2))
