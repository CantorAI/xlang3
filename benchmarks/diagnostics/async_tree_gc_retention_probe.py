"""Inspect tracked-object retention around the official async-tree workload.

The probe records object counts and process memory before and after an exact
pyperformance tree body, then calls gc.collect() and records them again. The
instrumentation changes runtime cost, so these values diagnose retention only;
they are not benchmark scores. Start with reduced levels, then try level 6.
"""

import argparse
import asyncio
import gc
import json
import os
import runpy
import sys
from pathlib import Path

import psutil
import pyperformance


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "benchmark",
    choices=("none", "eager", "io", "eager_io", "memoization",
             "eager_memoization", "cpu_io_mixed", "eager_cpu_io_mixed"),
)
parser.add_argument("--task-groups", action="store_true")
parser.add_argument("--levels", type=int, choices=range(1, 7), default=4)
parser.add_argument(
    "--no-memory",
    action="store_true",
    help="skip psutil memory snapshots when the runtime's psutil ABI is incomplete",
)
args = parser.parse_args()

benchmark_path = (
    Path(pyperformance.__file__).parent
    / "data-files"
    / "benchmarks"
    / "bm_async_tree"
    / "run_benchmark.py"
)
namespace = runpy.run_path(str(benchmark_path), run_name="async_tree_gc_probe")
# Coroutine methods resolve this global from their defining module at runtime.
namespace["AsyncTree"].recurse_with_gather.__globals__["NUM_RECURSE_LEVELS"] = args.levels
tree_type = namespace["BENCHMARKS"][args.benchmark]
process = None if args.no_memory else psutil.Process(os.getpid())


def snapshot():
    tracked = gc.get_objects()
    tracked_count = len(tracked)
    del tracked
    memory = process.memory_info() if process is not None else None
    return {
        "tracked_objects": tracked_count,
        "gc_count": list(gc.get_count()),
        "rss_bytes": memory.rss if memory is not None else None,
        "private_bytes": getattr(memory, "private", None) if memory is not None else None,
    }


before = snapshot()
tree = tree_type(use_task_groups=args.task_groups)
asyncio.run(tree.run())
del tree
after_workload = snapshot()
collected = gc.collect()
after_collection = snapshot()

print(json.dumps({
    "diagnostic_only": True,
    "python_version": sys.version,
    "executable": sys.executable,
    "benchmark": args.benchmark,
    "task_groups": args.task_groups,
    "levels": args.levels,
    "collected": collected,
    "before": before,
    "after_workload": after_workload,
    "after_collection": after_collection,
}, indent=2))
