"""Compare existing direct-method caches with inherited subscript dispatch."""
from collections import Counter
import json
import time


class DirectDict(dict):
    __getitem__ = dict.__getitem__
    __setitem__ = dict.__setitem__


class InheritedDict(DirectDict):
    pass


class DirectCounter(Counter):
    __getitem__ = dict.__getitem__
    __setitem__ = dict.__setitem__


class DirectPython:
    def __init__(self):
        self.values = {}

    def __getitem__(self, key):
        return self.values[key]

    def __setitem__(self, key, value):
        self.values[key] = value


class InheritedPython(DirectPython):
    pass


def sample(cls, keys):
    owner = cls()
    for key in keys:
        owner[key] = 0
    start = time.perf_counter()
    for unused in range(4):
        for key in keys:
            owner[key] += 1
    seconds = time.perf_counter() - start
    assert all(owner[key] == 4 for key in keys)
    return seconds


rows = []
for count in (256, 1024):
    keys = [(bytes([index & 255, index >> 8]), b'right') for index in range(count)]
    for label, cls in (('exact_dict', dict), ('direct_native_dict', DirectDict),
                       ('inherited_native_dict', InheritedDict),
                       ('inherited_native_counter', Counter), ('direct_native_counter', DirectCounter),
                       ('direct_python', DirectPython), ('inherited_python', InheritedPython)):
        sample(cls, keys)
        samples = [sample(cls, keys) for unused in range(5)]
        rows.append({'path': label, 'key_count': count, 'updates': count * 4,
                     'samples_seconds': samples, 'all_values_verified': True})
print(json.dumps({'purpose': 'subscript dispatch diagnosis only, never an official score', 'rows': rows}))
