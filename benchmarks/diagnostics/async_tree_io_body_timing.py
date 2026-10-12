"""Time one unmodified six-level pyperformance async-tree I/O body.

This diagnostic omits pyperf calibration and profiling instrumentation so a
single body can be compared while investigating cases that exceed the harness
cap. The result is not an official pyperformance score.
"""

import argparse
import asyncio
import json
import runpy
import sys
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("benchmark_source", type=Path)
parser.add_argument("--task-groups", action="store_true")
args = parser.parse_args()

namespace = runpy.run_path(str(args.benchmark_source), run_name="async_tree_io_timing")
tree = namespace["IOAsyncTree"](use_task_groups=args.task_groups)
started = time.perf_counter()
asyncio.run(tree.run())
elapsed = time.perf_counter() - started
print(json.dumps({
    "diagnostic_only": True,
    "benchmark": "async_tree_io",
    "levels": 6,
    "task_groups": args.task_groups,
    "executable": sys.executable,
    "elapsed_seconds": elapsed,
}, indent=2))
