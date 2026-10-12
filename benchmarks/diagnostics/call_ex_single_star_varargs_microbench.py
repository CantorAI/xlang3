"""Measure exact single-star calls to a Python varargs-only function."""

import time


def collect(*args):
    return args


items = (1, 2, 3, 4, 5, 6)


def run(count):
    result = None
    for _ in range(count):
        result = collect(*items)
    return result


run(100)
for repeat in range(5):
    start = time.perf_counter()
    result = run(50_000)
    elapsed = time.perf_counter() - start
    assert result == items
    print(f"single-star-varargs calls=50000 repeat={repeat + 1}: {elapsed:.6f}s")
