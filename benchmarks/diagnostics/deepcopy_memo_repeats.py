"""Repeated in-process diagnostic for pyperformance's deepcopy_memo body.

Each timed sample follows pyperformance 1.14.0's ``benchmark_memo(n)`` body:
it calls ``copy.deepcopy(data)`` in a loop with the same graph and aliases.
Only pyperf's worker/calibration layer is omitted. Use paired
control/candidate runs as a screening measurement, not as a replacement for
official pyperformance.
"""

import copy
import os
from statistics import median
from time import perf_counter


A = [1] * 100
data = {"a": (A, A, A), "b": [A] * 100}
probe = copy.deepcopy(data)
assert probe is not data
assert probe["a"][0] is probe["a"][1] is probe["a"][2]
assert probe["b"][0] is probe["a"][0] and probe["a"][0] is not A


SAMPLES = int(os.environ.get("DEEPCOPY_DIAG_SAMPLES", "9"))
LOOPS = int(os.environ.get("DEEPCOPY_DIAG_LOOPS", "500"))
times = []
for _ in range(SAMPLES):
    started = perf_counter()
    for _ in range(LOOPS):
        _ = copy.deepcopy(data)
    times.append((perf_counter() - started) / LOOPS)

print("deepcopy_memo median_seconds_per_copy", median(times))
print("deepcopy_memo samples_seconds_per_copy", " ".join(f"{x:.9f}" for x in times))
