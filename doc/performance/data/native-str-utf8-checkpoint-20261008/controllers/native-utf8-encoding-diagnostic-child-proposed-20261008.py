"""Unscored UTF-8 entry-point diagnostic; identical native API operations/data.

One untimed parity pass, then five samples of 20,000 operations per case.
No profiling, codec replacement, monkeypatching, workload tuning or acceptance.
"""
import _codecs
import hashlib
import json
import sys
import time

OPERATIONS = 20000
SAMPLES = 5
INPUTS = {
    'ascii': ('ASCII 0123456789\x00\n', b'ASCII 0123456789\x00\n'),
    'multibyte': ('A\u00e9\u4e2d\U0001f600\x00', b'A\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80\x00'),
    'surrogate': ('A\ud800\u00e9\udfff', b'A\xed\xa0\x80\xc3\xa9\xed\xbf\xbf'),
}
CASES = [(name, route) for name in INPUTS for route in
    (('method_surrogatepass', 'codecs_surrogatepass', 'method_default', 'codecs_strict')
     if name != 'surrogate' else ('method_surrogatepass', 'codecs_surrogatepass'))]


def signature(value):
    return {'length': len(value), 'sha256': hashlib.sha256(value).hexdigest(),
            'byte_checksum': sum(value)}


def operation(text, route):
    if route == 'method_surrogatepass':
        return text.encode('utf-8', 'surrogatepass')
    if route == 'codecs_surrogatepass':
        return _codecs.utf_8_encode(text, 'surrogatepass')[0]
    if route == 'method_default':
        return text.encode()
    assert route == 'codecs_strict'
    return _codecs.utf_8_encode(text, 'strict')[0]


def sample(text, route):
    # Route selection occurs before the timer; each loop spells the actual API.
    if route == 'method_surrogatepass':
        start = time.perf_counter()
        for _ in range(OPERATIONS):
            value = text.encode('utf-8', 'surrogatepass')
        elapsed = time.perf_counter() - start
    elif route == 'codecs_surrogatepass':
        start = time.perf_counter()
        for _ in range(OPERATIONS):
            value = _codecs.utf_8_encode(text, 'surrogatepass')[0]
        elapsed = time.perf_counter() - start
    elif route == 'method_default':
        start = time.perf_counter()
        for _ in range(OPERATIONS):
            value = text.encode()
        elapsed = time.perf_counter() - start
    else:
        assert route == 'codecs_strict'
        start = time.perf_counter()
        for _ in range(OPERATIONS):
            value = _codecs.utf_8_encode(text, 'strict')[0]
        elapsed = time.perf_counter() - start
    return elapsed, value


def main():
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.optimize == 0
    assert sys.getprofile() is None and sys.gettrace() is None
    source_hash = hashlib.sha256(open(__file__, 'rb').read()).hexdigest()
    expected = {name: {'codepoints': [ord(c) for c in text], **signature(value)}
                for name, (text, value) in INPUTS.items()}
    common = {'runtime': sys.implementation.name, 'executable': sys.executable,
              'version_info': list(sys.version_info[:3]), 'optimization_level': sys.flags.optimize,
              'profile_enabled': False, 'trace_enabled': False, 'child_sha256': source_hash,
              'operations_per_sample': OPERATIONS, 'samples_per_case': SAMPLES,
              'case_count': len(CASES), 'timed_operations': len(CASES) * SAMPLES * OPERATIONS,
              'input_signature': expected, 'scope': 'Unscored native API entry-point screen only'}
    print(json.dumps(dict(common, status='diagnostic_start')), flush=True)
    assert _codecs.__name__ == '_codecs' and _codecs.utf_8_encode.__name__ == 'utf_8_encode'
    assert callable(_codecs.utf_8_encode) and getattr(_codecs.utf_8_encode, '__code__', None) is None
    provider = {'module': _codecs.__name__, 'module_file': getattr(_codecs, '__file__', None),
                'function_name': _codecs.utf_8_encode.__name__,
                'function_type': type(_codecs.utf_8_encode).__name__, 'python_code': False}
    parity = []
    for name, route in CASES:
        text, expected_bytes = INPUTS[name]
        value = operation(text, route)
        assert type(value) is bytes and value == expected_bytes
        encoded, consumed = _codecs.utf_8_encode(text, 'surrogatepass')
        assert type(encoded) is bytes and encoded == expected_bytes and consumed == len(text)
        parity.append({'input': name, 'route': route, 'output_signature': signature(value)})
    # Default/strict reject the surrogate input; these exception checks are untimed.
    strict_errors = []
    for route in ('method_default', 'codecs_strict'):
        try:
            operation(INPUTS['surrogate'][0], route)
        except UnicodeEncodeError as error:
            assert error.encoding == 'utf-8' and error.start == 1 and error.end == 2
            assert error.object == INPUTS['surrogate'][0]
            strict_errors.append({'route': route, 'exception': type(error).__name__,
                                  'encoding': error.encoding, 'start': error.start, 'end': error.end})
        else:
            raise AssertionError('Strict encoding must reject the surrogate input')
    rows = []
    for index in range(SAMPLES):
        order = CASES if index % 2 == 0 else list(reversed(CASES))
        for name, route in order:
            elapsed, value = sample(INPUTS[name][0], route)
            assert elapsed > 0 and type(value) is bytes and value == INPUTS[name][1]
            # Checks/hashing occur after the timer, never once per timed operation.
            rows.append({'sample_index': index, 'input': name, 'route': route,
                         'elapsed_seconds': elapsed, 'output_signature': signature(value)})
    assert sys.getprofile() is None and sys.gettrace() is None
    assert hashlib.sha256(open(__file__, 'rb').read()).hexdigest() == source_hash
    print(json.dumps(dict(common, status='diagnostic_complete', success=True,
                         provider=provider, parity=parity, strict_errors=strict_errors,
                         samples=rows, child_hash_unchanged=True)), flush=True)


if __name__ == '__main__':
    main()
