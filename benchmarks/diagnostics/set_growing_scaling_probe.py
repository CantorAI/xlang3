"""Focused growth scaling, separate from the official pyperformance suite.

Input construction is outside the timed region. Run all runtimes on an idle
machine, retaining each stdout JSON. This probe does not establish suite wins.
"""

import json
import statistics
import time

records = []
for size in (1024, 4096, 16384):
    values = ["node-" + str(index) for index in range(size)]
    samples = []
    for repeat in range(5):
        start = time.perf_counter()
        seen = set()
        for value in values:
            if value not in seen:
                seen.add(value)
        elapsed = time.perf_counter() - start
        assert len(seen) == size
        samples.append(elapsed)
    records.append({"size": size, "median_seconds": statistics.median(samples),
                    "samples_seconds": samples})
print(json.dumps({"workload": "membership followed by unique insertion",
                  "repeats": 5, "records": records}, indent=2))
