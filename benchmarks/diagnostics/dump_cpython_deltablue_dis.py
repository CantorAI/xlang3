"""Warm pyperformance DeltaBlue and print CPython's adaptive disassembly.

Usage with CPython 3.14:

    python dump_cpython_deltablue_dis.py PATH_TO_BM_DELTABLUE/run_benchmark.py

The benchmark source is unchanged. Only pyperf.Runner is replaced so the
script can warm the benchmark function in one process and inspect its methods.
"""

import dis
import hashlib
from pathlib import Path
import platform
import runpy
import sys
import types


if len(sys.argv) != 2:
    raise SystemExit(__doc__)

benchmark = Path(sys.argv[1]).resolve()


class DirectRunner:
    def __init__(self, **_kwargs):
        self.metadata = {}

    def bench_func(self, name, function, *args, **kwargs):
        warm_iterations = 5
        print(f"WARMING {name}: {warm_iterations} iterations, arguments={args!r}")
        for _ in range(warm_iterations):
            function(*args, **kwargs)


pyperf_stub = types.ModuleType("pyperf")
pyperf_stub.Runner = DirectRunner
sys.modules["pyperf"] = pyperf_stub
sys.argv = [str(benchmark)]

print(f"CPython {platform.python_version()}")
print(f"benchmark_sha256={hashlib.sha256(benchmark.read_bytes()).hexdigest()}")
namespace = runpy.run_path(str(benchmark), run_name="__main__")

for class_name, method_name in (
    ("BinaryConstraint", "input"),
    ("BinaryConstraint", "output"),
    ("BinaryConstraint", "recalculate"),
    ("ScaleConstraint", "execute"),
    ("Strength", "stronger"),
    ("Strength", "weaker"),
):
    method = namespace[class_name].__dict__[method_name]
    if isinstance(method, (classmethod, staticmethod)):
        method = method.__func__
    print(f"\n===== {class_name}.{method_name} =====")
    dis.dis(method, adaptive=True, show_caches=True)
