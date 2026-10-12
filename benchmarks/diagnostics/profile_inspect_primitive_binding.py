"""Separate repeated descriptor lookup from the underlying inspect primitives."""

import json
import statistics
import sys
import time


class Sample:
    def __init__(self):
        self.a = self.b = self.c = self.d = self.e = 42


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 50_000
    instance = Sample()
    instance_dict = instance.__dict__
    sentinel = object()
    cls = type(instance)
    class_dict = cls.__dict__
    object_getattribute = object.__getattribute__
    dict_get = dict.get
    bound_dict_get = instance_dict.get
    type_dict_descriptor = type.__dict__["__dict__"]

    def run(operation):
        result = None
        started = time.perf_counter()
        for _ in range(loops):
            result = operation()
        return time.perf_counter() - started, result

    operations = {
        "object_getattribute_dynamic": lambda: object.__getattribute__(instance, "__dict__"),
        "object_getattribute_cached": lambda: object_getattribute(instance, "__dict__"),
        "dict_get_dynamic_descriptor": lambda: dict.get(instance_dict, "a", sentinel),
        "dict_get_cached_descriptor": lambda: dict_get(instance_dict, "a", sentinel),
        "dict_get_dynamic_bound": lambda: instance_dict.get("a", sentinel),
        "dict_get_cached_bound": lambda: bound_dict_get("a", sentinel),
        "class_dict_dynamic_descriptor": lambda: type(instance).__dict__,
        "class_dict_cached_descriptor": lambda: type_dict_descriptor.__get__(cls, type(cls)),
        "class_proxy_dynamic_item": lambda: class_dict["__init__"],
    }
    results = {}
    for name, operation in operations.items():
        samples = []
        for _ in range(7):
            elapsed, value = run(operation)
            samples.append(elapsed)
            assert value is not None
        results[name] = {
            "samples_seconds": samples,
            "median_seconds": statistics.median(samples),
            "loops": loops,
        }
    print(json.dumps({"diagnostic_only": True, "python_version": sys.version,
                      "loops": loops, "results": results}, indent=2))


if __name__ == "__main__":
    main()
