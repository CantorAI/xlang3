"""Measure the hot immediate-value isinstance call used by SciMark arrays.

This is a diagnostic microbenchmark, not an official pyperformance result.
The call shape mirrors ArrayList.__getitem__: integer row/column indexes are
checked against tuple before taking the scalar-indexing branch.
"""
import time


def check_isinstance_tuple(count):
    matches = 0
    for index in range(count):
        matches += isinstance(index, tuple)
    return matches


def check_isinstance_int(count):
    matches = 0
    for index in range(count):
        matches += isinstance(index, int)
    return matches


def check_isinstance_tuple_value(count):
    matches = 0
    for index in range(count):
        matches += isinstance((index, index), tuple)
    return matches


def check_type(count):
    result = None
    for index in range(count):
        result = type(index)
    return result


count = 1_000_000
checks = (
    ("isinstance-tuple", check_isinstance_tuple),
    ("isinstance-tuple-value", check_isinstance_tuple_value),
    ("isinstance-int", check_isinstance_int),
    ("type", check_type),
)
for name, check in checks:
    for _ in range(2):
        check(100_000)  # warm call-site caches
    for _ in range(3):
        started = time.perf_counter()
        result = check(count)
        elapsed = time.perf_counter() - started
        print(f"{name} {elapsed:.9f} {result}")
