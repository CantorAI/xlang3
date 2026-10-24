"""Fixed-count native string search scaling; not an official suite score."""
import json
import statistics
import time


def find_tail(text, size, reads):
    total = 0
    for _ in range(reads):
        total += text.find("\n", size - 2)
    return total


def rfind_tail(text, size, reads):
    total = 0
    for _ in range(reads):
        total += text.rfind("\n", size - 2)
    return total


def startswith_tail(text, size, reads):
    total = 0
    for _ in range(reads):
        total += text.startswith("\n", size - 1)
    return total


def count_empty(text, size, reads):
    total = 0
    for _ in range(reads):
        total += text.count("", 0)
    return total


records = []
reads = 64
for size in (256, 4096, 65536):
    for family in ("ascii", "sparse_multibyte", "dense_multibyte"):
        if family == "ascii":
            text = "a" * (size - 1) + "\n"
        elif family == "sparse_multibyte":
            text = "a" * (size - 3) + "\u00e9\U0001f600\n"
        else:
            text = "\u00e9" * (size - 2) + "\U0001f600\n"
        assert len(text) == size
        for operation, function, expected in (
            ("find_tail", find_tail, (size - 1) * reads),
            ("rfind_tail", rfind_tail, (size - 1) * reads),
            ("startswith_tail", startswith_tail, reads),
            ("count_empty", count_empty, (size + 1) * reads),
        ):
            assert function(text, size, reads) == expected
            samples = []
            for _ in range(5):
                start = time.perf_counter()
                result = function(text, size, reads)
                elapsed = time.perf_counter() - start
                assert result == expected
                samples.append(elapsed)
            records.append({"family": family, "operation": operation,
                            "source_characters": size, "reads": reads,
                            "checksum": expected, "samples_seconds": samples,
                            "median_seconds": statistics.median(samples)})
print(json.dumps({"diagnostic_only": True, "construction_timed": False,
                  "unmeasured_warmup_passes": 1, "repeats": 5,
                  "records": records}, indent=2))
