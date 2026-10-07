"""Focused native-array scalar-read scaling; not an official suite result.

Array construction is outside the timer. A scalar read should not copy the
entire backing buffer. Function locals keep module-name lookup out of the loop.
"""

import json
import statistics
import time
from array import array


def read_first(values, reads):
    total = 0.0
    for index in range(reads):
        total += values[0]
    return total


records = []
reads = 256
for size in (256, 4096, 65536):
    values = array("d", [0.5] * size)
    samples = []
    for repeat in range(5):
        start = time.perf_counter()
        total = read_first(values, reads)
        elapsed = time.perf_counter() - start
        assert total == reads * 0.5
        samples.append(elapsed)
    records.append({"array_elements": size, "reads": reads,
                    "median_seconds": statistics.median(samples),
                    "samples_seconds": samples})
print(json.dumps({"diagnostic_only": True, "repeats": 5,
                  "workload": "fixed number of scalar reads at index zero",
                  "records": records}, indent=2))
