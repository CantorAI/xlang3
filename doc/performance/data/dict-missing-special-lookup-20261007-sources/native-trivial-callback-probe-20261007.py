"""Checked call-route diagnosis only; no official workload score."""
from collections import Counter
import json
import time


def zero(value):
    return 0


def direct_constant(keys, mapping, rounds):
    total = 0
    for unused in range(rounds):
        for key in keys:
            total += zero(key)
    return total


def native_constant(keys, mapping, rounds):
    total = 0
    for unused in range(rounds):
        selected = max(keys, key=zero)
        assert selected is keys[0]
        total += 1
    return total


def counter_missing(keys, mapping, rounds):
    total = 0
    for unused in range(rounds):
        for key in keys:
            total += mapping[key]
    return total


def direct_missing(keys, mapping, rounds):
    total = 0
    missing = mapping.__missing__
    for unused in range(rounds):
        for key in keys:
            total += missing(key)
    return total


def dict_default(keys, mapping, rounds):
    total = 0
    for unused in range(rounds):
        for key in keys:
            total += mapping.get(key, 0)
    return total


keys = [(bytes([index]), b'right') for index in range(256)]
rows = []
rounds = 32
for label, function, mapping, expected in (
        ('direct_constant', direct_constant, {}, 0),
        ('native_max_constant_key', native_constant, {}, rounds),
        ('counter_missing', counter_missing, Counter(), 0),
        ('direct_counter_missing', direct_missing, Counter(), 0),
        ('dict_get_default', dict_default, {}, 0)):
    assert function(keys, mapping, rounds) == expected
    samples = []
    for unused in range(5):
        start = time.perf_counter()
        actual = function(keys, mapping, rounds)
        samples.append(time.perf_counter() - start)
        assert actual == expected
    assert len(mapping) == 0
    rows.append({'path': label, 'operations_per_sample': rounds * len(keys),
                 'samples_seconds': samples, 'checked_result': expected, 'mapping_stays_empty': True})
print(json.dumps({'scope': 'Call route diagnosis; distinct loop shapes, never official speed scores', 'rows': rows}))
