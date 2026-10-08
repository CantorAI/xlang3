"""Immutable hash caching must preserve key equality and construction lifetimes."""
import marshal
import pickle
import sys

payload = b'payload' * 32
keys = [payload, (payload, b'right'), ((payload, 7), (None, True, 2 ** 80))]
for key in keys:
    expected = hash(key)
    for unused in range(5):
        assert hash(key) == expected
    assert {key: 'value'}[key] == 'value'
print('bytes and nested immutable tuple hashes stay stable: OK')

mapping = {(True, payload): 1}
mapping[(1, payload)] += 1
assert len(mapping) == 1 and mapping[(1.0, payload)] == 2
for unused in range(5):
    try:
        hash(([], payload))
    except TypeError:
        pass
    else:
        raise AssertionError('unhashable tuple member accepted')
print('equal mixed numeric keys and failed hashes keep their semantics: OK')

for index in range(1000):
    key = (payload, index)
    assert {key: index}[(payload, index)] == index
    assert hash(key) == hash((payload, index))
    del key
    original = bytearray([index & 255])
    frozen = bytes(original)
    expected = hash(frozen)
    original[0] = (index + 1) & 255
    assert frozen == bytes([index & 255]) and hash(frozen) == expected
print('recycled tuples and fresh bytes never retain earlier payload hashes: OK')

for original in keys:
    expected = hash(original)
    restored = marshal.loads(marshal.dumps(original))
    assert restored == original and hash(restored) == expected
    restored = pickle.loads(pickle.dumps(original))
    assert restored == original and hash(restored) == expected
print('marshal and pickle reconstruction hash the completed contents: OK')

events = []


class Custom:
    def __hash__(self):
        events.append('hash')
        return 7


custom = Custom()
assert hash((custom,)) == hash((7,))
assert events == ['hash'], events
print('unsupported tuple members retain Python hash dispatch: OK')

traced = []


def trace(frame, event, argument):
    if event == 'call' and frame.f_code.co_name == '__hash__':
        traced.append(event)
    return trace


sys.settrace(trace)
try:
    assert hash((Custom(),)) == hash((7,))
finally:
    sys.settrace(None)
assert traced == ['call'], traced
print('user hash callbacks retain Python tracing: OK')
