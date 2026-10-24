"""Native regex position/ownership scaling; diagnostic, not a suite score."""
import json
import re
import statistics
import time

pattern = re.compile(r"([0-9]+)(x?)")
reads = 64
records = []


def scan(text, start):
    total = 0
    for _ in range(reads):
        match = pattern.match(text, start)
        assert match.group(1) == "123456" and match.group(2) == ""
        total += match.start() + match.end() + len(match.group())
    return total


for size in (256, 4096, 65536):
    for family in ("ascii", "sparse_multibyte", "dense_multibyte"):
        if family == "ascii":
            text = "a" * (size - 6) + "123456"
        elif family == "sparse_multibyte":
            text = "a" * (size - 8) + "\u00e9\U0001f600123456"
        else:
            text = "\u00e9" * (size - 7) + "\U0001f600123456"
        assert len(text) == size
        expected = 2 * size * reads
        assert scan(text, size - 6) == expected
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            result = scan(text, size - 6)
            elapsed = time.perf_counter() - start
            assert result == expected
            samples.append(elapsed)
        records.append({"family": family, "source_characters": size,
                        "reads": reads, "checksum": expected,
                        "samples_seconds": samples,
                        "median_seconds": statistics.median(samples)})
print(json.dumps({"diagnostic_only": True, "construction_timed": False,
                  "unmeasured_warmup_passes": 1, "repeats": 5,
                  "records": records}, indent=2))
