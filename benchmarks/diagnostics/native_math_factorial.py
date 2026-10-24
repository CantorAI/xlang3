"""Isolate math.factorial's native cost after the full-suite runs finish.

These samples include Python loop/call overhead and are diagnostic evidence,
not official pyperformance scores. In particular, they do not establish how
much of an async-tree result is spent in factorial or task scheduling.
"""
import json
import math
import sys
import time


class Indexed:
    def __init__(self, value):
        self.value = value
        self.calls = 0

    def __index__(self):
        self.calls += 1
        return self.value


class IntOnly:
    def __int__(self):
        return 5


class BadIndex:
    def __index__(self):
        return 2.5


class RaisingIndex:
    def __index__(self):
        raise RuntimeError('index sentinel')


class IntegerSubclass(int):
    def __mul__(self, other):
        raise AssertionError('factorial must use the integer value')


def expect_error(argument, error_type):
    try:
        math.factorial(argument)
    except error_type:
        return
    raise AssertionError('missing expected factorial error')


def validate():
    # Independent Python recurrence checks every small/int-to-bigint boundary,
    # plus the exact input used by pyperformance's mixed async-tree workload.
    # Do this outside timing; never replace factorial with the reference loop.
    reference = 1
    selected = (0, 1, 2, 20, 21, 63, 64, 65, 100, 500, 1000, 5000)
    checked = 0
    for n in range(5001):
        if n:
            reference *= n
        if n <= 100 or n in selected:
            assert math.factorial(n) == reference, n
            checked += 1
        if n in selected:
            assert math.perm(n) == reference, n
    indexed = Indexed(500)
    assert math.factorial(indexed) == math.factorial(500)
    assert indexed.calls == 1
    assert math.factorial(IntegerSubclass(21)) == math.factorial(21)
    assert math.factorial(False) == 1 and math.factorial(True) == 1
    for value in (-1, -(1 << 100)):
        expect_error(value, ValueError)
    for value in (2.0, '5', None, IntOnly(), BadIndex()):
        expect_error(value, TypeError)
    expect_error(1 << 100, OverflowError)
    expect_error(RaisingIndex(), RuntimeError)
    return checked


def run():
    base_loops = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    assert base_loops > 0
    checked = validate()
    rows = []
    factorial = math.factorial
    for n in (0, 20, 100, 500, 1000, 5000):
        loops = max(10, base_loops * 500 // max(500, n))
        for _ in range(min(100, loops)):
            factorial(n)
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            for _ in range(loops):
                result = factorial(n)
            samples.append(time.perf_counter() - start)
        rows.append({'n': n, 'loops': loops, 'seconds': samples,
                     'median_seconds': sorted(samples)[2],
                     'result_bit_length': result.bit_length(),
                     'result_modulo_97': result % 97})
    print(json.dumps({'purpose': 'diagnostic only; Python loop/call overhead included',
                      'correctness_inputs_checked': checked, 'rows': rows}))


if __name__ == '__main__':
    run()
