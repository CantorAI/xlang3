"""Execute one pyperformance workload without pyperf's worker/process setup.

Use this only for XLang3 diagnostics (for example ``--perf-counters``). The
benchmark source is executed unchanged; only its ``pyperf.Runner`` entry point
is replaced so the workload runs once in-process. Results are not timings for
comparison with pyperf.
"""
import runpy
import sys
import types
import time


if len(sys.argv) != 2:
    raise SystemExit("usage: run_pyperformance_workload_direct.py BENCHMARK.py")


class _DirectRunner:
    def __init__(self):
        self.metadata = {}

    def bench_func(self, name, function, *args, **kwargs):
        # Do not loop or calibrate: pyperformance's function and arguments stay
        # intact, while the worker/psutil setup is deliberately bypassed.
        started = time.perf_counter()
        result = function(*args, **kwargs)
        elapsed = time.perf_counter() - started
        print(f"direct workload {name}: {result!r} ({elapsed:.9f} s)")


pyperf_stub = types.ModuleType("pyperf")
pyperf_stub.Runner = _DirectRunner
sys.modules["pyperf"] = pyperf_stub
runpy.run_path(sys.argv[1], run_name="__main__")
