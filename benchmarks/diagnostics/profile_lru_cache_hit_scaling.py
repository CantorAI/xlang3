"""Measure native functools LRU-cache hits at increasing working-set sizes.

This diagnostic isolates the hit lookup from the official benchmark. The
multi-argument keys mirror inspect._shadowed_dict's cached weakref-MRO tuples.
Run unchanged under CPython 3.14.7 and XLang3; pyperformance remains the gate.
"""
import json
import sys
import time
from functools import lru_cache


loops = int(sys.argv[1]) if len(sys.argv) > 1 else 2_000


@lru_cache(maxsize=128)
def cached(left, right):
    return left ^ right


rows = []
for size in (1, 8, 32, 128):
    keys = [(index, index + 1) for index in range(size)]
    for left, right in keys:
        cached(left, right)
    start = time.perf_counter()
    hits = 0
    for _ in range(loops):
        for left, right in keys:
            hits += cached(left, right)
    rows.append({"working_set": size, "hits": loops * size,
                 "seconds": time.perf_counter() - start,
                 "checksum": hits})
print(json.dumps({"loops": loops, "rows": rows}))
