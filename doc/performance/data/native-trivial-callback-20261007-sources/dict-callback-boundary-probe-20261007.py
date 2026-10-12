"""Diagnose dict lookup versus Python calls and native builtin callbacks."""
from collections import Counter
import json
import time


def make_callback(mapping):
    def lookup(key):
        return mapping[key]
    return lookup


def direct_lookup(keys, mapping, callback, rounds):
    checksum = 0
    for unused in range(rounds):
        best = -1
        for key in keys:
            value = mapping[key]
            if value > best:
                best = value
        checksum += best
    return checksum


def python_call(keys, mapping, callback, rounds):
    checksum = 0
    for unused in range(rounds):
        best = -1
        for key in keys:
            value = callback(key)
            if value > best:
                best = value
        checksum += best
    return checksum


def native_callback(keys, mapping, callback, rounds):
    checksum = 0
    for unused in range(rounds):
        best_key = max(keys, key=callback)
        checksum += mapping[best_key]
    return checksum


keys = [(bytes([index & 255, index >> 8]), b'right') for index in range(1024)]
rows = []
rounds = 8
for kind in ('dict', 'Counter'):
    mapping = {} if kind == 'dict' else Counter()
    for index, key in enumerate(keys):
        mapping[key] = index * 17 % 1024
    callback = make_callback(mapping)
    for label, function in (('direct_lookup', direct_lookup), ('python_call', python_call),
                            ('native_callback', native_callback)):
        assert function(keys, mapping, callback, rounds) == rounds * 1023
        samples = []
        for unused in range(5):
            start = time.perf_counter()
            checksum = function(keys, mapping, callback, rounds)
            samples.append(time.perf_counter() - start)
            assert checksum == rounds * 1023
        rows.append({'mapping': kind, 'path': label, 'lookups_per_sample': rounds * len(keys),
                     'samples_seconds': samples, 'checksum': checksum})
print(json.dumps({'purpose': 'callback boundary diagnosis only, not an official score', 'rows': rows}))
