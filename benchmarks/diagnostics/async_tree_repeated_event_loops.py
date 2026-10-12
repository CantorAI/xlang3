"""Repeat the official pyperformance eager async-tree body across fresh loops.

This mirrors the pyperf async runner lifecycle so loop reuse and cumulative
Task/Future state can be separated from a single tree execution. Progress is
printed around each loop to make a stall's last completed phase explicit.
"""
import asyncio
import importlib.util
import os
from pathlib import Path
import time

import pyperformance


benchmark_path = (
    Path(pyperformance.__file__).parent
    / "data-files"
    / "benchmarks"
    / "bm_async_tree"
    / "run_benchmark.py"
)
spec = importlib.util.spec_from_file_location("bm_async_tree", benchmark_path)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load official benchmark source: {benchmark_path}")
benchmark_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark_module)

tree = benchmark_module.EagerAsyncTree()
repeat_count = int(os.environ.get("XLANG3_ASYNC_TREE_REPEATS", "12"))
for run_number in range(1, repeat_count + 1):
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    started = time.perf_counter()
    print(f"run {run_number} start", flush=True)
    try:
        loop.run_until_complete(tree.run())
        print(
            f"run {run_number} complete {time.perf_counter() - started:.6f}s",
            flush=True,
        )
    finally:
        asyncio.set_event_loop(None)
        loop.close()
        print(f"run {run_number} loop closed", flush=True)
