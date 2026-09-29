import json
import json.encoder
import time

empty = {}
simple = {'key1': 0, 'key2': True, 'key3': 'value', 'key4': 'foo', 'key5': 'string'}
nested = {'key1': 0, 'key2': simple, 'key3': 'value', 'key4': simple, 'key5': simple, 'key': '\u0105\u0107\u017c'}
data = [(empty, 2000), (simple, 1000), (nested, 1000), ([nested] * 1000, 1)]
stats = {}
for name in ('encode', 'iterencode'):
    original = getattr(json.JSONEncoder, name)
    stat = [0, 0.0]
    def make_wrapper(name, original, stat):
        def wrapped(self, *args, **kwargs):
            start = time.perf_counter()
            try:
                return original(self, *args, **kwargs)
            finally:
                stat[0] += 1
                stat[1] += time.perf_counter() - start
        return wrapped
    setattr(json.JSONEncoder, name, make_wrapper(name, original, stat))
    stats[name] = stat
start = time.perf_counter()
for obj, count in data:
    for _ in range(count):
        json.dumps(obj)
whole = time.perf_counter() - start
print('whole_ms', round(whole * 1000, 3))
for name, (calls, seconds) in stats.items():
    print(name, calls, round(seconds * 1000, 3))
