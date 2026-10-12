"""Measure inspect's real weakref-MRO LRU hit path without a profiler.

Uses CPython 3.14's inspect._shadowed_dict_from_weakref_mro_tuple cache and
the same call shape on XLang3. This isolates cache key hashing/lookup from the
rest of inspect.getattr_static; it is diagnostic, not a pyperformance score.
"""
import inspect
import json
import sys
import time
import weakref


target_hits = int(sys.argv[1]) if len(sys.argv) > 1 else 128_000
cache = inspect._shadowed_dict_from_weakref_mro_tuple
rows = []

for size in (1, 8, 32, 128):
    classes = [type(f"Probe{size}_{i}", (), {}) for i in range(size)]
    keys = [tuple(weakref.ref(base) for base in cls.__mro__) for cls in classes]
    cache.cache_clear()
    for key in keys:
        cache(*key)
    start = time.perf_counter()
    loops = target_hits // size
    for _ in range(loops):
        for key in keys:
            cache(*key)
    elapsed = time.perf_counter() - start
    rows.append({"working_set": size, "hits": loops * size,
                 "seconds": elapsed, "cache_info": str(cache.cache_info())})

print(json.dumps({"target_hits_per_working_set": target_hits, "rows": rows}))
