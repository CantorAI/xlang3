"""Compare exception and missing-static-attribute costs across runtimes."""
import inspect
import json
import sys
import time


class Empty:
    pass


obj = Empty()
loops = int(sys.argv[1]) if len(sys.argv) > 1 else 20_000


def construct_only():
    AttributeError("missing")


def raise_and_catch():
    try:
        raise AttributeError("missing")
    except AttributeError:
        pass


def getattr_with_default():
    return getattr(obj, "missing", None)


def static_with_default():
    return inspect.getattr_static(obj, "missing", None)


def static_raise_and_catch():
    try:
        inspect.getattr_static(obj, "missing")
    except AttributeError:
        pass


rows = []
for name, function in (
    ("construct_only", construct_only),
    ("raise_and_catch", raise_and_catch),
    ("getattr_with_default", getattr_with_default),
    ("static_with_default", static_with_default),
    ("static_raise_and_catch", static_raise_and_catch),
):
    for _ in range(100):
        function()
    start = time.perf_counter()
    for _ in range(loops):
        function()
    rows.append({"case": name, "loops": loops,
                 "seconds": time.perf_counter() - start})
print(json.dumps({"purpose": "diagnostic only", "rows": rows}))
