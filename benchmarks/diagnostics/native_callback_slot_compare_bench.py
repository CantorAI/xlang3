"""Time repeated Python slot/double rich comparisons through native `_heapq`.

This targeted microbenchmark is directional evidence, not a pyperformance
score. It mutates the two slot values identically in both runtimes so each
iteration exercises the comparison result and native heap callback path.
"""

import argparse
import asyncio
import json
import statistics
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--loops", type=int, default=300_000)
parser.add_argument("--repeat", type=int, default=5)
args = parser.parse_args()

loop = asyncio.new_event_loop()
left = asyncio.TimerHandle(1.0, lambda: None, (), loop)
right = asyncio.TimerHandle(2.0, lambda: None, (), loop)
samples = []
for _ in range(args.repeat):
    start = time.perf_counter()
    checksum = 0
    for _ in range(args.loops):
        checksum += left < right
        left._when, right._when = right._when, left._when
    samples.append(time.perf_counter() - start)
loop.close()

print(json.dumps({
    "diagnostic_only": True,
    "benchmark": "native_callback_slot_double_compare",
    "executable": sys.executable,
    "loops_per_sample": args.loops,
    "repeat": args.repeat,
    "calls": args.loops * args.repeat,
    "checksum": checksum,
    "samples_seconds": samples,
    "median_seconds": statistics.median(samples),
}, indent=2))
