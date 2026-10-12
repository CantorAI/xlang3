"""Compare atomic and container cases through copy.deepcopy itself."""
import copy
import statistics
import time

CASES = (
    ('atomic_int', 1),
    ('atomic_str', 'a short string'),
    ('small_list', [1, 2, 3]),
    ('small_dict', {'a': 1, 'b': 2}),
)
CALLS = 5000
REPEATS = 5
for name, value in CASES:
    samples = []
    for _ in range(REPEATS):
        start = time.perf_counter()
        for _ in range(CALLS):
            copy.deepcopy(value)
        samples.append(time.perf_counter() - start)
    median = statistics.median(samples)
    print(f'{name} n={CALLS}: median={median:.6f}s ({median * 1e6 / CALLS:.3f} us/call)')
