"""Unscored identical Python calls: VM loop versus native sorted callbacks.

Imports/input construction/output checking stay outside each body timer. These
cases diagnose invocation costs; they are not pyperformance suite scores.
"""
import time

COUNT = 50000
items = list(range(COUNT))

def small(value):
    return value + 1

def branch(value):
    if value < 0:
        return 0
    return value + 1

def loop():
    total = 0
    for value in range(COUNT):
        total += value + 1
    return total

def small_calls():
    total = 0
    for value in range(COUNT):
        total += small(value)
    return total

def branch_calls():
    total = 0
    for value in range(COUNT):
        total += branch(value)
    return total

def sort_plain():
    return sorted(items)

def sort_callback():
    return sorted(items, key=branch)

for name, body, expected in (
    ("loop", loop, COUNT * (COUNT + 1) // 2),
    ("small_calls", small_calls, COUNT * (COUNT + 1) // 2),
    ("branch_calls", branch_calls, COUNT * (COUNT + 1) // 2),
    ("sort_plain", sort_plain, items),
    ("sort_callback", sort_callback, items),
):
    assert body() == expected
    for sample in range(3):
        start = time.perf_counter()
        result = body()
        elapsed = time.perf_counter() - start
        assert result == expected
        print("callback_boundary", name, sample, COUNT, elapsed)
