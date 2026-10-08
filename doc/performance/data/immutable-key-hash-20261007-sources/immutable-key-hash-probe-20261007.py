"""Unrun diagnosis: isolate immutable hash cost after the live official run.

Hash values differ between runtimes. Check stability within each process rather
than treating any implementation's hash value as a cross-runtime checksum.
Use original Counter/update diagnostics separately to relate this to BPE.
"""
import json
import time

OPERATIONS = 8192


def measure(shape, width, fresh, repetitions):
    payloads = [bytes([index]) * width for index in range(256)]
    keys = payloads if shape == 'bytes' else [(item, b'right') for item in payloads]
    expected = [hash(key) for key in keys]
    before = time.perf_counter()
    checks = 0
    for index in range(OPERATIONS):
        slot = index & 255
        original = keys[slot]
        key = (original[0], original[1]) if fresh else original
        for unused in range(repetitions):
            assert hash(key) == expected[slot]
            checks += 1
    elapsed = time.perf_counter() - before
    assert checks == OPERATIONS * repetitions
    return elapsed, checks


rows = []
for shape, width, fresh, repetitions in (
    ('bytes', 1, False, 1), ('bytes', 32, False, 1), ('bytes', 256, False, 1),
    ('tuple_bytes', 1, False, 1), ('tuple_bytes', 32, False, 1),
    ('tuple_bytes', 256, False, 1), ('tuple_bytes', 32, True, 1),
    ('tuple_bytes', 32, True, 2),
):
    measure(shape, width, fresh, repetitions)
    samples, checksums = [], []
    for unused in range(5):
        elapsed, checks = measure(shape, width, fresh, repetitions)
        samples.append(elapsed)
        checksums.append(checks)
    rows.append({'shape': shape, 'bytes_width': width, 'fresh_tuple': fresh,
                 'hashes_per_key': repetitions, 'operations': OPERATIONS,
                 'samples_seconds': samples, 'checked_hash_calls': checksums})
print(json.dumps({'scope': 'diagnostic, includes shared Python loop/assertion overhead', 'rows': rows}, sort_keys=True))
