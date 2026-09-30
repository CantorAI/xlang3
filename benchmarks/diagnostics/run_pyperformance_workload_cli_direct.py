"""Run one pyperformance CLI benchmark body directly in a runtime.

This harness bypasses pyperf's worker setup but leaves the benchmark script,
options, and workload body intact. Its timings are diagnostic, not comparable
to official pyperf results. Invoke as:

    runtime run_pyperformance_workload_cli_direct.py BENCHMARK.py [arguments]
"""
import argparse
import os
import runpy
import sys
import time
import types


if len(sys.argv) < 2:
    raise SystemExit("usage: run_pyperformance_workload_cli_direct.py BENCHMARK.py [arguments]")

benchmark_path = sys.argv[1]
benchmark_args = sys.argv[2:]
sys.argv = [benchmark_path, *benchmark_args]


class _DirectRunner:
    def __init__(self, **_kwargs):
        self.metadata = {}
        self.argparser = argparse.ArgumentParser(add_help=False)

    def parse_args(self):
        return self.argparser.parse_args()

    def bench_func(self, name, function, *args, **kwargs):
        loops = max(1, int(os.environ.get("PYPERF_DIRECT_LOOPS", "1")))
        started = time.perf_counter()
        result = None
        for _ in range(loops):
            result = function(*args, **kwargs)
        elapsed = time.perf_counter() - started
        print(
            f"direct workload {name}: {result!r} total={elapsed:.9f} s "
            f"loops={loops} per_loop={elapsed / loops:.9f} s"
        )

    def bench_time_func(self, name, function, *args, **kwargs):
        # pyperf's inner_loops multiplies work inside one benchmark iteration;
        # honor it while omitting worker calibration and subprocess setup.
        inner_loops = int(kwargs.pop("inner_loops", 1))
        loops = max(1, int(os.environ.get("PYPERF_DIRECT_LOOPS", "1")))
        started = time.perf_counter()
        result = function(loops, *args, **kwargs)
        print(f"direct workload {name}: {result!r} wall={time.perf_counter() - started:.9f} s loops={loops} inner_loops={inner_loops}")


pyperf_stub = types.ModuleType("pyperf")
pyperf_stub.Runner = _DirectRunner
pyperf_stub.perf_counter = time.perf_counter
pyperf_stub.python_implementation = lambda: "CPython"
sys.modules["pyperf"] = pyperf_stub
runpy.run_path(benchmark_path, run_name="__main__")
