"""Checked own/inherited/ordinary slot reads; diagnostic, not official score."""
import json
import time

class Own:
    __slots__ = ('padding', 'x')

class Inherited(Own):
    __slots__ = ('extra',)

class Ordinary:
    pass

def read(item, count):
    total = 0
    for unused in range(count):
        total += item.x
    return total

rows = []
for label, kind in (('own_slot', Own), ('inherited_slot', Inherited), ('ordinary_attr', Ordinary)):
    item = kind()
    item.x = 7
    assert read(item, 16384) == 16384 * 7
    samples = []
    for unused in range(5):
        start = time.perf_counter()
        checksum = read(item, 16384)
        samples.append(time.perf_counter() - start)
        assert checksum == 16384 * 7
    rows.append({'path': label, 'operations': 16384, 'checksum': checksum, 'samples_seconds': samples})
print(json.dumps({'purpose': 'checked attribute diagnostic, not official score', 'rows': rows}))
