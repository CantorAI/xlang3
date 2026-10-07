"""Count benchmark-file Python call events without pyperf worker setup.

The target benchmark is executed unchanged; only ``pyperf.Runner`` is replaced
to profile one invocation in-process. Profiling changes timings substantially,
so this is for call-frequency diagnostics only, not performance comparisons.
"""
import json
import os
import runpy
import sys
import time
import types
from collections import Counter


if len(sys.argv) < 2:
    raise SystemExit("usage: profile_pyperformance_calls_direct.py BENCHMARK.py [benchmark arguments]")

benchmark_path = sys.argv[1]
benchmark_args = sys.argv[2:]
sys.argv = [benchmark_path, *benchmark_args]

class _ProfilingRunner:
    def __init__(self, **_kwargs):
        self.metadata = {}
        self.argparser = types.SimpleNamespace(add_argument=lambda *_args, **_kwargs: None)

    def parse_args(self):
        pure_python = "--pure-python" in benchmark_args
        protocol = None
        benchmark = None
        for index, arg in enumerate(benchmark_args):
            if arg == "--protocol" and index + 1 < len(benchmark_args):
                protocol = int(benchmark_args[index + 1])
            elif not arg.startswith("-") and arg != "pickle":
                benchmark = arg
            elif arg == "pickle":
                benchmark = arg
        return types.SimpleNamespace(pure_python=pure_python,
                                     protocol=protocol,
                                     benchmark=benchmark)

    def bench_func(self, name, function, *args, **kwargs):
        calls = Counter()
        inclusive_seconds = Counter()
        self_seconds = Counter()
        stack = []

        def profile(frame, event, arg):
            if event == "call":
                stack.append([frame, time.perf_counter(), 0.0])
                return
            if event != "return" or not stack or stack[-1][0] is not frame:
                return
            finished = time.perf_counter()
            current = stack.pop()
            elapsed = finished - current[1]
            exclusive = elapsed - current[2]
            if stack:
                stack[-1][2] += elapsed
            filename = frame.f_code.co_filename.replace("\\", "/")
            # Attribute work often lives in typing.py/inspect.py or runtime
            # support modules rather than the pyperformance benchmark file.
            # Keep source basename with each function to expose those paths.
            key = (filename.rsplit("/", 1)[-1], frame.f_code.co_name)
            calls[key] += 1
            inclusive_seconds[key] += elapsed
            self_seconds[key] += exclusive

        # Explicit warmup separates steady-state call frequencies from lazy
        # imports (for example typing's first import of inspect). Profiling still
        # changes execution; these are diagnostics, never pyperf scores.
        warmups = max(0, int(os.environ.get("PYPERF_PROFILE_WARMUP_CALLS", "0")))
        for _ in range(warmups):
            function(*args, **kwargs)
        print("diagnostic unprofiled warmup calls", warmups)
        sys.setprofile(profile)
        try:
            result = function(*args, **kwargs)
        finally:
            sys.setprofile(None)
        print("profiled workload", name, repr(result))
        print("profiled Python calls, grouped by source/function")
        print(json.dumps(calls.most_common(60), separators=(",", ":")))
        timing = sorted(
            ((function, calls[function], inclusive_seconds[function], self_seconds[function])
             for function in calls),
            key=lambda row: row[2], reverse=True)
        print("profiled benchmark function times (inclusive/self seconds)")
        for (filename, function), count, inclusive, exclusive in timing[:40]:
            print(filename, function, count, round(inclusive, 6), round(exclusive, 6))
        print("profiled benchmark-frame calls", sum(calls.values()))

    def bench_time_func(self, name, function, *args, **kwargs):
        # A single body pass is enough to reveal hot call sites. Skip pyperf's
        # inner-loop multiplier because profiling every call is deliberately
        # expensive and the timing is not used as a benchmark result.
        kwargs.pop("inner_loops", None)
        loops = max(1, int(os.environ.get("PYPERF_DIRECT_LOOPS", "1")))
        return self.bench_func(name, lambda: function(loops, *args, **kwargs))


pyperf_stub = types.ModuleType("pyperf")
pyperf_stub.Runner = _ProfilingRunner
pyperf_stub.python_implementation = lambda: "CPython"
pyperf_stub.perf_counter = time.perf_counter
sys.modules["pyperf"] = pyperf_stub
runpy.run_path(benchmark_path, run_name="__main__")
