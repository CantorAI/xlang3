"""Measure ordinary built-in dict iterator creation and next costs."""

import argparse
import json
import statistics
import time


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--size", type=int, default=10_000)
parser.add_argument("--passes", type=int, default=50)
parser.add_argument("--repeats", type=int, default=7)
args = parser.parse_args()

source = {index: index + 1 for index in range(args.size)}


def consume(iterable):
    count = 0
    for _ in iterable:
        count += 1
    return count


cases = {
    "keys": lambda: consume(source.keys()),
    "values": lambda: consume(source.values()),
    "items": lambda: consume(source.items()),
    "list": lambda: consume(list(source)),
}
results = {}
for name, operation in cases.items():
    expected = args.size
    assert operation() == expected
    samples = []
    for _ in range(args.repeats):
        start = time.perf_counter()
        for _ in range(args.passes):
            count = operation()
        samples.append(time.perf_counter() - start)
        assert count == expected
    results[name] = {
        "samples_seconds": samples,
        "median_seconds": statistics.median(samples),
        "items_per_sample": args.size * args.passes,
    }

print(json.dumps({
    "diagnostic_only": True,
    "size": args.size,
    "passes": args.passes,
    "repeats": args.repeats,
    "results": results,
}, indent=2))
