"""Measure native Context.run calling back into an ordinary Python function.

Run unchanged with CPython 3.14.7 and the fixed XLang3 Release executable.
The direct-call loop is the control; both loops execute the same callback body.
"""

import statistics
import time
from contextvars import Context


def callback(value):
    return value


class SlotBox:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


context = Context()
box = SlotBox(7)


def direct_call(count):
    result = None
    for value in range(count):
        result = callback(value)
    return result


def context_call(count):
    result = None
    for value in range(count):
        result = context.run(callback, value)
    return result


def attribute_read(count):
    obj = box
    result = None
    for _ in range(count):
        result = obj.value
    return result


def method_call(count):
    obj = box
    result = None
    for _ in range(count):
        result = obj.get()
    return result


def measure(function, count=100_000, repeats=7):
    function(1_000)
    samples = []
    for _ in range(repeats):
        start = time.perf_counter()
        result = function(count)
        elapsed = time.perf_counter() - start
        expected = count - 1 if function in (direct_call, context_call) else 7
        if result != expected:
            raise AssertionError(f"wrong result: {result!r}")
        samples.append(elapsed)
    return statistics.median(samples), samples


for name, function in (
    ("direct_call", direct_call),
    ("context_run", context_call),
    ("attribute_read", attribute_read),
    ("method_call", method_call),
):
    median, samples = measure(function)
    print(f"{name}: median={median:.6f}s samples=" + ",".join(f"{x:.6f}" for x in samples))
