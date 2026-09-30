"""Compare the public json.dumps path with direct _json encoder calls.

This is a call-path diagnostic, not a pyperf benchmark. It uses pyperformance
1.14's four payloads and repetition counts, and reports medians from seven
timed passes so wrapper cost can be separated from native encoder cost.
"""
import csv
import json
import statistics
import sys
import time

import _json


SIMPLE = {
    "key1": 0,
    "key2": True,
    "key3": "value",
    "key4": "foo",
    "key5": "string",
}
NESTED = {
    "key1": 0,
    "key2": SIMPLE,
    "key3": "value",
    "key4": SIMPLE,
    "key5": SIMPLE,
    "key": "ąćż",
}
CASES = (
    ("EMPTY", {}, 2000),
    ("SIMPLE", SIMPLE, 1000),
    ("NESTED", NESTED, 1000),
    ("HUGE", [NESTED] * 1000, 1),
)
native_encoder = _json.make_encoder(
    {}, None, _json.encode_basestring_ascii, None, ": ", ", ", False, False, True
)
MODES = (
    ("json.dumps", lambda value: json.dumps(value)),
    ("JSONEncoder.encode", lambda value: json._default_encoder.encode(value)),
    (
        "iterencode+join",
        lambda value: "".join(json._default_encoder.iterencode(value, True)),
    ),
    ("direct _json+join", lambda value: "".join(native_encoder(value, 0))),
)


writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(("runtime", "case", "calls", "path", "median_ms", "median_us_per_call"))
for case_name, value, calls in CASES:
    for path_name, function in MODES:
        function(value)
        samples = []
        for _ in range(7):
            start = time.perf_counter()
            for _ in range(calls):
                result = function(value)
            samples.append(time.perf_counter() - start)
        median_seconds = statistics.median(samples)
        writer.writerow((
            sys.implementation.name,
            case_name,
            calls,
            path_name,
            f"{median_seconds * 1000:.6f}",
            f"{median_seconds * 1_000_000 / calls:.3f}",
        ))
