"""Unprofiled generic hash-callback cost diagnostic, never an official score.

Run serially under the parent's idle guard. No library body is replaced. All
hash results are checked against the runtime's own string hash so random hash
seeds cannot change the cross-runtime workload/checksum identities.
"""
import json
import sys
from time import perf_counter

OPERATIONS = 16384
SAMPLES = 5
WARMUP_OPERATIONS = 256
KEY_TEXT = 'SELECT'


class PythonKey:
    def __init__(self, name):
        self._name_ = name

    def __hash__(self):
        return hash(self._name_)


def ordinary_python_hash(key):
    return hash(key._name_)


def ordinary_python_hash_forward(key):
    return hash(key)


def string_dict_get(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += mapping.get(key, 0)
    return checksum


def python_key_dict_get(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += mapping.get(key, 0)
    return checksum


def builtin_hash_string(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += hash(key) == expected
    return checksum


def builtin_hash_python_key(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += hash(key) == expected
    return checksum


def direct_python_hash_method(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += key.__hash__() == expected
    return checksum


def saved_python_hash_method(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += saved_method() == expected
    return checksum


def ordinary_python_hash_function(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += ordinary_python_hash(key) == expected
    return checksum


def ordinary_python_hash_wrapper(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += ordinary_python_hash_forward(key) == expected
    return checksum


def main():
    if sys.implementation.name == 'cpython':
        assert sys.version_info[:3] == (3, 14, 7), 'Use exact CPython 3.14.7'
        assert sys.executable.replace('\\', '/').lower() == 'c:/python/python314/python.exe'
    else:
        assert sys.implementation.name == 'xlang3'
    key = PythonKey(KEY_TEXT)
    expected = hash(KEY_TEXT)
    assert hash(key) == expected and key.__hash__() == expected
    string_mapping = {KEY_TEXT: 17}
    python_mapping = {key: 17}
    saved_method = key.__hash__
    cases = [
        ('string_dict_get', string_dict_get, KEY_TEXT, string_mapping, 17),
        ('python_key_dict_get', python_key_dict_get, key, python_mapping, 17),
        ('builtin_hash_string', builtin_hash_string, KEY_TEXT, None, 1),
        ('builtin_hash_python_key', builtin_hash_python_key, key, None, 1),
        ('direct_python_hash_method', direct_python_hash_method, key, None, 1),
        ('saved_python_hash_method', saved_python_hash_method, key, None, 1),
        ('ordinary_python_hash_function', ordinary_python_hash_function, key, None, 1),
        ('ordinary_python_hash_wrapper', ordinary_python_hash_wrapper, key, None, 1),
    ]
    rows = []
    for name, body, operand, mapping, multiplier in cases:
        assert body(WARMUP_OPERATIONS, operand, mapping, expected, saved_method) == WARMUP_OPERATIONS * multiplier
        times = []
        for _ in range(SAMPLES):
            started = perf_counter()
            checksum = body(OPERATIONS, operand, mapping, expected, saved_method)
            elapsed = perf_counter() - started
            assert checksum == OPERATIONS * multiplier and elapsed > 0
            times.append(elapsed)
        rows.append({'path': name, 'operations': OPERATIONS, 'checksum': OPERATIONS * multiplier,
                     'key_text': KEY_TEXT, 'samples_seconds': times})
    print(json.dumps({'purpose': 'generic unprofiled callback diagnostic only; not pyperformance',
                      'runtime': sys.implementation.name, 'version': sys.version,
                      'operations_per_sample': OPERATIONS, 'samples_per_row': SAMPLES,
                      'warmup_operations_per_row': WARMUP_OPERATIONS,
                      'instrumentation': 'none', 'rows': rows}))


if __name__ == '__main__':
    main()
