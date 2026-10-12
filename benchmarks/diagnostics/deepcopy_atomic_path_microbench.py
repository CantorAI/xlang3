import copy
import statistics
import time

VALUE = 1
CALLS = 10000
REPEATS = 9
CASES = (
    ("type", lambda: type(VALUE)),
    ("atomic_membership", lambda: type(VALUE) in copy._atomic_types),
    ("dispatch_get", lambda: copy._deepcopy_dispatch.get(type(VALUE))),
    ("full_deepcopy", lambda: copy.deepcopy(VALUE)),
)
for name, fn in CASES:
    fn()
    samples = []
    for _ in range(REPEATS):
        start = time.perf_counter()
        for _ in range(CALLS):
            fn()
        samples.append(time.perf_counter() - start)
    median = statistics.median(samples)
    print(f"{name} calls={CALLS} median={median:.6f}s us/call={median * 1e6 / CALLS:.3f}")
