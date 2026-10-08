"""Run serially after both full suites; diagnostic timings, not suite scores.

Construct keys outside the timer. Preserve the Python Counter implementation.
Each sample does the same number of updates per key and verifies every result.
Save raw samples; compare unchanged candidate/control/CPython 3.14.7 separately.
"""
import collections
import json
import time


def make_keys(count, shape):
    binary = [bytes([index & 255, index >> 8]) for index in range(count)]
    if shape == 'bytes':
        return binary
    return [(key, b'right') for key in binary]


def sample(count, shape, counter, fresh_pair, rounds):
    keys = make_keys(count, shape)
    mapping = collections.Counter() if counter else {}
    for key in keys:
        mapping[key] = 0
    before = time.perf_counter()
    for unused in range(rounds):
        for original in keys:
            # Recreate the pair to match the BPE read/modify/write key shape.
            # The same fresh pair participates in this read and this write.
            key = (original[0], original[1]) if fresh_pair else original
            mapping[key] += 1
    elapsed = time.perf_counter() - before
    assert len(mapping) == count
    assert all(mapping[key] == rounds for key in keys)
    assert list(mapping) == keys
    checksum = sum(mapping.values())
    assert checksum == count * rounds
    return elapsed, checksum


rows = []
rounds = 4
for shape, counter, fresh_pair in (
        ('bytes', False, False),
        ('tuple_bytes', False, False),
        ('tuple_bytes', False, True),
        ('tuple_bytes', True, True)):
    for count in (64, 256, 1024):
        # One untimed-result warmup followed by three separately recorded
        # executions. No calibration, significance claim, or pyperf score.
        sample(count, shape, counter, fresh_pair, rounds)
        samples = []
        checksums = []
        for unused in range(3):
            seconds, checksum = sample(count, shape, counter, fresh_pair, rounds)
            samples.append(seconds)
            checksums.append(checksum)
        rows.append({'shape': shape, 'counter': counter, 'fresh_pair': fresh_pair,
                     'key_count': count, 'updates': count * rounds,
                     'samples_seconds': samples, 'checksums': checksums})
print(json.dumps({'purpose': 'generic dictionary scaling diagnosis only',
                  'rounds': rounds, 'rows': rows}, sort_keys=True))
