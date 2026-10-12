"""Time inspect's static-lookup building blocks without a profiler.

Diagnostic only. Run under CPython 3.14.7 and XLang3 with the same loop count
to separate instance-dict access, inspect helpers, and full static lookup.
"""
import inspect
import json
import sys
import time


class Many:
    def __init__(self):
        self.a = self.b = self.c = self.d = self.e = 42


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 20_000
    instance = Many()
    instance_class = type(instance)
    class_dictionary = instance_class.__dict__
    sentinel = object()
    functions = [
        ("object_getattribute_dict", lambda: object.__getattribute__(instance, "__dict__")),
        ("dict_get", lambda: dict.get(instance.__dict__, "a", sentinel)),
        ("inspect_check_instance", lambda: inspect._check_instance(instance, "a")),
        ("inspect_check_class", lambda: inspect._check_class(type(instance), "a")),
        ("inspect_getattr_static", lambda: inspect.getattr_static(instance, "a")),
        ("inspect_shadowed_dict", lambda: inspect._shadowed_dict(type(instance))),
        ("class_dict_proxy_getter", lambda: type(instance).__dict__),
        ("class_dict_contains_fresh_proxy", lambda: "__init__" in type(instance).__dict__),
        ("class_dict_contains_cached_proxy", lambda: "__init__" in class_dictionary),
        ("class_dict_get_fresh_proxy", lambda: type(instance).__dict__["__init__"]),
        ("class_dict_get_cached_proxy", lambda: class_dictionary["__init__"]),
        ("inspect_static_getmro", lambda: inspect._static_getmro(type(instance))),
    ]
    rows = []
    for name, function in functions:
        for _ in range(100):
            function()
        start = time.perf_counter()
        for _ in range(loops):
            result = function()
        rows.append({"case": name, "loops": loops,
                     "seconds": time.perf_counter() - start,
                     "result_type": type(result).__name__})
    print(json.dumps({"purpose": "diagnostic only; includes Python call/loop overhead",
                      "rows": rows}))


if __name__ == "__main__":
    main()
