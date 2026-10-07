"""Repeated string indexing/length/slice scaling, not an official suite score.

Construction and one unmeasured warmup are outside the timer. The fixed read
count isolates source-size sensitivity; samples measure a warmed runtime path.
The official Tomli case must still measure its own index-construction cost.
"""
import json
import statistics
import time


def read_first(text, size, reads):
    total = 0
    for _ in range(reads):
        total += ord(text[0])
    return total


def read_last(text, size, reads):
    total = 0
    for _ in range(reads):
        total += ord(text[-1])
    return total


def read_length(text, size, reads):
    total = 0
    for _ in range(reads):
        total += len(text)
    return total


def read_tail_slice(text, size, reads):
    total = 0
    for _ in range(reads):
        total += len(text[size - 2:size])
    return total


records = []
reads = 256
for size in (256, 4096, 65536):
    for family in ("ascii", "sparse_multibyte", "dense_multibyte"):
        if family == "ascii":
            text = "a" * size
            first, last, tail = 97, 97, "aa"
        elif family == "sparse_multibyte":
            text = "a" * (size - 2) + "\u00e9\U0001f600"
            first, last, tail = 97, 128512, "\u00e9\U0001f600"
        else:
            text = "\u00e9" * (size - 1) + "\U0001f600"
            first, last, tail = 233, 128512, "\u00e9\U0001f600"
        source_bytes = len(text.encode("utf-8"))
        assert text[0] == chr(first) and text[-1] == chr(last)
        assert text[size - 2:size] == tail and len(text) == size
        for operation, function, expected in (
            ("first_scalar", read_first, reads * first),
            ("last_scalar", read_last, reads * last),
            ("length", read_length, reads * size),
            ("tail_slice", read_tail_slice, reads * 2),
        ):
            assert function(text, size, reads) == expected
            samples = []
            for _ in range(5):
                start = time.perf_counter()
                result = function(text, size, reads)
                elapsed = time.perf_counter() - start
                assert result == expected
                samples.append(elapsed)
            records.append({
                "family": family, "operation": operation,
                "source_characters": size, "source_utf8_bytes": source_bytes,
                "reads": reads, "checksum": expected,
                "median_seconds": statistics.median(samples),
                "samples_seconds": samples,
            })
print(json.dumps({
    "diagnostic_only": True, "construction_timed": False,
    "unmeasured_warmup_passes": 1, "repeats": 5,
    "workload": "fixed-count warmed string indexing, length, and short slices",
    "records": records,
}, indent=2))
