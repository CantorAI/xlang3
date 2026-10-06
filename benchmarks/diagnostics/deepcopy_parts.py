import runpy
import sys
import time
import types

if len(sys.argv) != 2:
    raise SystemExit('usage: deepcopy_parts.py PATH_TO_RUN_BENCHMARK')
pyperf = types.ModuleType('pyperf')
pyperf.perf_counter = time.perf_counter
sys.modules['pyperf'] = pyperf
bench = runpy.run_path(sys.argv[1])
for name, loops in [('benchmark', 30), ('benchmark_reduce', 100), ('benchmark_memo', 2)]:
    start = time.perf_counter()
    reported = bench[name](loops)
    elapsed = time.perf_counter() - start
    print(f'{name} loops={loops}: elapsed={elapsed:.6f}s body={reported:.6f}s')
