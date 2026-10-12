"""Time only the official async_tree_none body, excluding interpreter startup."""

import asyncio
import runpy
import sys
import time

if len(sys.argv) != 2:
    raise SystemExit("usage: time_async_tree_none_body.py PATH_TO_RUN_BENCHMARK_PY")

namespace = runpy.run_path(sys.argv[1], run_name="xlang3_async_tree_timed")
workload = namespace["NoneAsyncTree"]()
start = time.perf_counter()
asyncio.run(workload.run())
elapsed = time.perf_counter() - start
print(f"async_tree_none_body_seconds={elapsed:.9f}")
