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


class Wrapper:
    def __init__(self, values):
        self.values = values

    def __getitem__(self, index):
        return self.values[index]

    def __setitem__(self, index, value):
        self.values[index] = value


class CheckedRows:
    def __init__(self, rows):
        self.rows = rows

    def __getitem__(self, index):
        # SciMark's ArrayList uses this same Python dispatch shape. Keep both
        # branches in Python; this probe isolates the false integer type check.
        if isinstance(index, tuple):
            return self.rows[index[1]][index[0]]
        return self.rows[index]


def read_rows(values, count):
    total = 0.0
    for index in range(count):
        total += values[index & 7][0]
    return total


def read_row_method(method, count):
    total = 0.0
    for index in range(count):
        total += method(index & 7)[0]
    return total


def count_typechecks(expected_type, count):
    matched = 0
    for index in range(count):
        if isinstance(index, expected_type):
            matched += 1
    return matched


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--operations", type=int, default=100000)
parser.add_argument("--repeats", type=int, default=7)
parser.add_argument("--primitive-row-checks", action="store_true",
                    help="Add native type-check and Python row-dispatch diagnostics")
args = parser.parse_args()
values = array.array("d", [1.0] * 256)
items = [1.0] * 256
wrapped = Wrapper(values)
cases = (
    ("array_getitem", read_subscription, values, float(args.operations)),
    ("array_saved_getitem", read_method, values.__getitem__, float(args.operations)),
    ("list_getitem", read_subscription, items, float(args.operations)),
    ("array_setitem", write_subscription, values, 1.0),
    ("array_saved_setitem", write_method, values.__setitem__, None),
    ("list_setitem", write_subscription, items, 1.0),
    ("python_getitem", read_subscription, wrapped, float(args.operations)),
    ("python_saved_getitem", read_method, wrapped.__getitem__, float(args.operations)),
    ("python_setitem", write_subscription, wrapped, 1.0),
    ("python_saved_setitem", write_method, wrapped.__setitem__, None),
)
if args.primitive_row_checks:
    rows = [array.array("d", [1.0] * 8) for _ in range(8)]
    checked_rows = CheckedRows(rows)
    cases += (
        ("primitive_typecheck_false", count_typechecks, tuple, 0),
        ("primitive_typecheck_true", count_typechecks, int, args.operations),
        ("python_checked_row_getitem", read_rows, checked_rows, float(args.operations)),
        ("python_checked_row_saved_getitem", read_row_method,
         checked_rows.__getitem__, float(args.operations)),
        ("list_row_getitem", read_rows, rows, float(args.operations)),
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
