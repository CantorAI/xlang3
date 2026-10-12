import runpy
import sys
import time
import types

_pyperf_stub = types.ModuleType("pyperf")
_pyperf_stub.perf_counter = time.perf_counter
sys.modules["pyperf"] = _pyperf_stub
_benchmark = runpy.run_path(
    r"C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_raytrace\run_benchmark.py",
    run_name="raytrace_pair_target",
)

def main():
    _benchmark["bench_raytrace"](1, 100, 100, None)

main()
