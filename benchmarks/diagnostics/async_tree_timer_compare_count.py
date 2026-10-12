"""Count TimerHandle comparisons in the official async_tree_io workload.

The wrapper changes execution cost, so elapsed time is diagnostic only. Run
with CPython 3.14.7 or XLang3 using the same pyperformance benchmark source to
compare comparison counts, not to produce a performance score.
"""

import argparse
import asyncio
import runpy
import time


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("benchmark_source", help="path to bm_async_tree/run_benchmark.py")
parser.add_argument("--task-groups", action="store_true")
parser.add_argument(
    "--levels",
    type=int,
    choices=range(1, 7),
    help="override the official six recursion levels for a smaller diagnostic run",
)
args = parser.parse_args()

namespace = runpy.run_path(args.benchmark_source, run_name="async_tree_timer_count")
if args.levels is not None:
    # The benchmark's coroutine methods resolve this module global at runtime,
    # so smaller trees retain the same scheduling and comparison code paths.
    # runpy returns a copy of its globals; use the function's actual namespace.
    benchmark_globals = namespace["AsyncTree"].recurse_with_gather.__globals__
    benchmark_globals["NUM_RECURSE_LEVELS"] = args.levels
counts = {"timer_lt": 0}
original_lt = asyncio.events.TimerHandle.__lt__


def counted_timer_lt(self, other):
    counts["timer_lt"] += 1
    return original_lt(self, other)


asyncio.events.TimerHandle.__lt__ = counted_timer_lt
tree = namespace["IOAsyncTree"](use_task_groups=args.task_groups)
started = time.perf_counter()
try:
    asyncio.run(tree.run())
finally:
    elapsed = time.perf_counter() - started
    asyncio.events.TimerHandle.__lt__ = original_lt

print({
    "runtime": "XLang3 or CPython; see sys.executable",
    "executable": __import__("sys").executable,
    "task_groups": args.task_groups,
    "timer_handle_lt_calls": counts["timer_lt"],
    "elapsed_seconds_diagnostic_only": elapsed,
})
