"""Split inspect.getattr_static's hot helpers for protocol benchmarks.

Uses CPython's unchanged inspect.py and the official typing benchmark classes.
Results identify runtime costs; this is diagnostic rather than pyperf scoring.
"""
import inspect
import json
import runpy
import sys
import time
import weakref
from pathlib import Path


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    benchmark = Path(sys.argv[2])
    namespace = runpy.run_path(str(benchmark), run_name="inspect_static_profile")
    instance = namespace["ManyInstanceVars"]()
    klass = type(instance)
    instance_dict = object.__getattribute__(instance, "__dict__")
    class_dict_get = inspect._get_dunder_dict_of_class
    class_dict = class_dict_get(klass)
    class_member = klass.__mro__[0]
    class_member_dict = class_dict_get(class_member)
    reference = weakref.ref(klass)
    cases = (
        ("weakref_ref_cached", lambda: weakref.ref(klass)),
        ("inspect_shadowed_dict", lambda: inspect._shadowed_dict(klass)),
        ("inspect_check_instance", lambda: inspect._check_instance(instance, "a")),
        ("inspect_check_class", lambda: inspect._check_class(klass, "a")),
        ("inspect_getattr_static_instance", lambda: inspect.getattr_static(instance, "a")),
        ("inspect_getattr_static_missing", lambda: inspect.getattr_static(instance, "missing", None)),
        ("object_getattribute_dict", lambda: object.__getattribute__(instance, "__dict__")),
        ("dict_get_instance_key", lambda: dict.get(instance_dict, "a", None)),
        ("type_dict_descriptor", lambda: class_dict_get(klass)),
        ("mappingproxy_contains", lambda: "a" in class_member_dict),
        ("mappingproxy_getitem", lambda: class_member_dict["__module__"]),
    )
    rows = []
    assert reference() is klass
    for name, function in cases:
        for _ in range(100):
            result = function()
        start = time.perf_counter()
        for _ in range(loops):
            result = function()
        rows.append({"case": name, "loops": loops,
                     "seconds": time.perf_counter() - start,
                     "result_type": type(result).__name__})
    print(json.dumps({"purpose": "diagnostic only; includes Python call overhead",
                      "rows": rows}))


if __name__ == "__main__":
    main()
