"""Long-sample confirmation of the unchanged string-dict lookup body only."""
import json
import sys
from time import perf_counter
OPERATIONS = 262144
SAMPLES = 21
WARMUP_OPERATIONS = 16384
KEY_TEXT = 'SELECT'

def string_dict_get(count, key, mapping, expected, saved_method):
    checksum = 0
    for _ in range(count):
        checksum += mapping.get(key, 0)
    return checksum

def main():
    assert sys.implementation.name == 'xlang3'
    mapping = {KEY_TEXT: 17}
    assert string_dict_get(WARMUP_OPERATIONS, KEY_TEXT, mapping, 17, None) == WARMUP_OPERATIONS * 17
    samples = []
    for _ in range(SAMPLES):
        started = perf_counter()
        checksum = string_dict_get(OPERATIONS, KEY_TEXT, mapping, 17, None)
        elapsed = perf_counter() - started
        assert checksum == OPERATIONS * 17 and elapsed > 0
        samples.append(elapsed)
    print(json.dumps({'instrumentation': 'none', 'purpose': 'long-sample negative-control confirmation, no official score',
        'rows': [{'path': 'string_dict_get', 'operations': OPERATIONS, 'checksum': checksum,
                  'key_text': KEY_TEXT, 'samples_seconds': samples}]}))
main()
