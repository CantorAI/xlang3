"""Compare the hit and first-miss branches of copy._keep_alive."""
import copy
import statistics
import time

CALLS = 1000
REPEATS = 7
value = [1, 2, 3]
hit_memo = {}
hit_memo[id(hit_memo)] = []
for _ in range(100):
    copy._keep_alive(value, hit_memo)

hit_samples = []
miss_samples = []
for _ in range(REPEATS):
    started = time.perf_counter()
    for _ in range(CALLS):
        copy._keep_alive(value, hit_memo)
    hit_samples.append(time.perf_counter() - started)

    # Allocate distinct empty memos before timing so the miss measurement
    # includes only the helper's first-insertion path.
    memos = [{} for _ in range(CALLS)]
    started = time.perf_counter()
    for memo in memos:
        copy._keep_alive(value, memo)
    miss_samples.append(time.perf_counter() - started)

hit = statistics.median(hit_samples)
miss = statistics.median(miss_samples)
print(f"runtime={__import__('sys').implementation.name} calls={CALLS} repeats={REPEATS}")
print(f"hit median={hit:.6f}s ({hit * 1e6 / CALLS:.3f} us/call)")
print(f"miss median={miss:.6f}s ({miss * 1e6 / CALLS:.3f} us/call)")
