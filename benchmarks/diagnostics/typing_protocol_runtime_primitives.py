"""Steady-state runtime primitive diagnostic; not an official pyperf score."""
import inspect
import json
import sys
import time
import weakref


class Base:
    value = 42


class Child(Base):
    pass


mro_get = type.__dict__["__mro__"].__get__
ref = weakref.ref(Child)


def noop(value):
    return value


def run():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    assert loops > 0
    cases = [
        ("python_call", noop, Child),
        ("native_mro_getter", mro_get, Child),
        ("native_weakref_create", weakref.ref, Child),
        ("native_weakref_deref", ref, None),
        ("inspect_shadowed_dict", inspect._shadowed_dict, Child),
    ]
    rows = []
    for name, function, argument in cases:
        for _ in range(1000):
            result = function() if argument is None else function(argument)
        start = time.perf_counter()
        for _ in range(loops):
            result = function() if argument is None else function(argument)
        elapsed = time.perf_counter() - start
        rows.append({"case": name, "loops": loops, "seconds": elapsed})
    print(json.dumps({"purpose": "diagnostic only, includes loop/call overhead",
                      "mro_identity_stable": mro_get(Child) is mro_get(Child),
                      "rows": rows}))


if __name__ == "__main__":
    run()
