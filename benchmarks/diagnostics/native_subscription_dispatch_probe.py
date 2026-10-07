"""Isolate subscription dispatch; diagnostic timings, not pyperformance scores.

Construction and method binding happen before timing. The read bodies differ
only in subscription versus a saved native method. List access supplies a
control for the loop itself. Keep array arithmetic and SciMark in Python.
"""
import argparse
import array
import json
import statistics
import time


def read_subscription(values, count):
    total = 0.0
    for index in range(count):
        total += values[index & 255]
    return total


def read_method(method, count):
    total = 0.0
    for index in range(count):
        total += method(index & 255)
    return total


def write_subscription(values, count):
    for index in range(count):
        values[index & 255] = 1.0
    return values[255]


def write_method(method, count):
    for index in range(count):
        method(index & 255, 1.0)
    return None


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--operations", type=int, default=100000)
parser.add_argument("--repeats", type=int, default=7)
args = parser.parse_args()
values = array.array("d", [1.0] * 256)
items = [1.0] * 256
cases = (
    ("array_getitem", read_subscription, values, float(args.operations)),
    ("array_saved_getitem", read_method, values.__getitem__, float(args.operations)),
    ("list_getitem", read_subscription, items, float(args.operations)),
    ("array_setitem", write_subscription, values, 1.0),
    ("array_saved_setitem", write_method, values.__setitem__, None),
    ("list_setitem", write_subscription, items, 1.0),
)
records = []
for name, body, operand, expected in cases:
    assert body(operand, args.operations) == expected
    samples = []
    for _ in range(args.repeats):
        started = time.perf_counter()
        result = body(operand, args.operations)
        samples.append(time.perf_counter() - started)
        assert result == expected
    records.append({"case": name, "operations": args.operations,
                    "median_seconds": statistics.median(samples),
                    "samples_seconds": samples})
print(json.dumps({"diagnostic_only": True, "records": records}, indent=2))
