"""Count callbacks scheduled by the official async_tree_none benchmark.

This is a diagnostic only. Wrapping call_soon changes timings, so do not use
the result as a benchmark score.
"""

import asyncio
import collections
import runpy
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: async_tree_call_soon_profile.py PATH_TO_RUN_BENCHMARK_PY")

counts = collections.Counter()
original_call_soon = asyncio.BaseEventLoop.call_soon


def observed_call_soon(self, callback, *args, **kwargs):
    owner = getattr(callback, "__self__", None)
    owner_type = (
        f"{type(owner).__module__}.{type(owner).__qualname__}"
        if owner is not None else "-"
    )
    callback_name = getattr(
        callback, "__qualname__", getattr(callback, "__name__", type(callback).__qualname__)
    )
    counts[(owner_type, callback_name)] += 1
    return original_call_soon(self, callback, *args, **kwargs)


asyncio.BaseEventLoop.call_soon = observed_call_soon
namespace = runpy.run_path(sys.argv[1], run_name="xlang3_async_tree_profile")
asyncio.run(namespace["NoneAsyncTree"]().run())

print("call_soon total", sum(counts.values()))
for (owner, name), count in counts.most_common(20):
    print(count, owner, name)
