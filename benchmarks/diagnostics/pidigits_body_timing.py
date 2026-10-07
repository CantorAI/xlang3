"""Paired diagnostic timing of the unchanged official body, not a suite score."""
import hashlib
import json
import runpy
import sys
import time

namespace = runpy.run_path(sys.argv[1], run_name='pidigits_timing_target')
calculate = namespace['calc_ndigits']
expected = 'e7cb4bbb129d29f035c29cf1f088673788a82ebbbd61b466257e12ece4871aac'
for _ in range(2):
    digits = calculate(2000)
    assert hashlib.sha256(bytes(digits)).hexdigest() == expected
samples = []
for _ in range(3):
    start = time.perf_counter()
    digits = calculate(2000)
    samples.append(time.perf_counter() - start)
    assert hashlib.sha256(bytes(digits)).hexdigest() == expected
print(json.dumps({'seconds': samples, 'median_seconds': sorted(samples)[1],
                  'digits_sha256': expected, 'purpose': 'diagnostic only; unchanged official body'}))
