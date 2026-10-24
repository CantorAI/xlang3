"""Isolate VM attribute/descriptor work in inspect's instance-dict probe.

Diagnostic only. Run unchanged under CPython 3.14.7 and XLang3. The direct
attribute, getattr, explicit object.__getattribute__, and cached descriptor
rows distinguish normal LOAD_ATTR from the generic descriptor-call path used
by inspect.getattr_static; they are not pyperformance scores.
"""
import json
import sys
import time


class Record:
    def __init__(self):
        self.a = self.b = self.c = self.d = self.e = 42


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    samples = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    instance = Record()
    explicit_getattribute = object.__getattribute__
    dict_get = dict.get
    marker = object()
    instance_dict = instance.__dict__
    class_dict_descriptor = Record.__dict__.get("__dict__", None)
    cases = (
        ("direct_instance_dict", lambda: instance.__dict__),
        ("getattr_instance_dict", lambda: getattr(instance, "__dict__")),
        ("explicit_getattribute_dynamic", lambda: object.__getattribute__(instance, "__dict__")),
        ("explicit_getattribute_cached", lambda: explicit_getattribute(instance, "__dict__")),
        ("dict_get_dynamic_descriptor", lambda: dict.get(instance.__dict__, "a", marker)),
        ("dict_get_cached_descriptor", lambda: dict_get(instance_dict, "a", marker)),
        ("direct_instance_item", lambda: instance.a),
    )
    rows = []
    for name, operation in cases:
        operation()
        timings = []
        for _ in range(samples):
            started = time.perf_counter()
            for _ in range(loops):
                value = operation()
            timings.append(time.perf_counter() - started)
        rows.append({"case": name, "loops": loops, "samples_seconds": timings,
                     "minimum_seconds": min(timings), "result_type": type(value).__name__})
    print(json.dumps({"purpose": "diagnostic only; includes loop/call overhead",
                      "python_version": sys.version, "loops": loops,
                      "samples": samples,
                      "class_dict_descriptor_type": type(class_dict_descriptor).__name__,
                      "class_dict_descriptor_repr": repr(class_dict_descriptor),
                      "rows": rows}))


if __name__ == "__main__":
    main()
