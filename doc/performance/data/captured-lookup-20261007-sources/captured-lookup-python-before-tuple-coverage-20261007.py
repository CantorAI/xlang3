"""Native callback IR shortcuts must retain dynamic Python lookup semantics."""
from collections import Counter
import contextvars
import sys
import traceback


def factory(mapping):
    def lookup(key):
        return mapping[key]

    def replace(value):
        nonlocal mapping
        mapping = value

    return lookup, replace


context = contextvars.Context()
token = object()
for mapping in ({'a': token, 'b': token}, Counter({'a': 3, 'b': 9})):
    lookup, replace = factory(mapping)
    assert context.run(lookup, 'a') is mapping['a']
    mapping['a'] = 17
    assert context.run(lookup, 'a') == 17
    replace({'a': 23})
    assert context.run(lookup, 'a') == 23
lookup, replace = factory(Counter({'a': 3, 'b': 9}))
assert max(['a', 'b'], key=lookup) == 'b'
assert context.run(lookup, 'missing') == 0
print('live captured cells hits and Counter misses preserve results: OK')


class Override(dict):
    def __getitem__(self, key):
        return 71


class Missing(dict):
    def __missing__(self, key):
        return 81


lookup, replace = factory(Override({'a': 1}))
assert context.run(lookup, 'a') == 71
Override.__getitem__ = lambda self, key: 73
assert context.run(lookup, 'a') == 73
replace(Missing())
assert context.run(lookup, 'absent') == 81
print('getitem replacement and missing hooks use original Python dispatch: OK')


class Descriptor:
    def __get__(self, instance, owner):
        return lambda key: 91


class Described(dict):
    __getitem__ = Descriptor()


lookup, replace = factory(Described({'a': 1}))
assert context.run(lookup, 'a') == 91
events = []


class Key:
    def __hash__(self):
        events.append('hash')
        return 13

    def __eq__(self, other):
        events.append('equal')
        return isinstance(other, Key)


original, query = Key(), Key()
lookup, replace = factory({original: 101})
events.clear()
assert context.run(lookup, query) == 101
assert 'hash' in events and 'equal' in events
print('descriptors and user key protocols retain observable callbacks: OK')

lookup, replace = factory({'a': 1})
try:
    context.run(lookup, 'absent')
except KeyError as error:
    assert traceback.extract_tb(error.__traceback__)[-1].name == 'lookup'
else:
    raise AssertionError('missing key accepted')
for args in ((), ('a', 'b')):
    try:
        context.run(lookup, *args)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid arity accepted')
print('fallible lookups and argument errors retain the Python call boundary: OK')


def changed_factory(mapping):
    def increment(key):
        return mapping[key] + 1
    return increment


lookup.__code__ = changed_factory({'a': 1}).__code__
assert context.run(lookup, 'a') == 2
try:
    raise ValueError('caller')
except ValueError as error:
    assert context.run(lookup, 'a') == 2
    assert sys.exception() is error
print('code replacement and caller handled exceptions remain visible: OK')

lookup, replace = factory({'a': 11, 'b': 13})
observed = []


def hook(frame, event, argument):
    if frame.f_code.co_name == 'lookup' and event in ('call', 'return'):
        observed.append(event)
    return hook


for setter in (sys.settrace, sys.setprofile):
    observed.clear()
    setter(hook)
    try:
        assert max(['a', 'b'], key=lookup) == 'b'
    finally:
        setter(None)
    assert observed == ['call', 'return', 'call', 'return'], observed
print('trace and profile observe every captured native callback: OK')

monitoring = sys.monitoring
monitored = []


def start(code, offset):
    monitored.append('start')


def finish(code, offset, value):
    assert value == 11
    monitored.append('return')


monitoring.use_tool_id(3, 'native-captured-lookup')
try:
    monitoring.register_callback(3, monitoring.events.PY_START, start)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, finish)
    monitoring.set_local_events(3, lookup.__code__, monitoring.events.PY_START | monitoring.events.PY_RETURN)
    assert context.run(lookup, 'a') == 11
    assert monitored == ['start', 'return']
finally:
    monitoring.set_local_events(3, lookup.__code__, 0)
    monitoring.register_callback(3, monitoring.events.PY_START, None)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, None)
    monitoring.free_tool_id(3)
print('monitoring observes captured lookup function entry and return: OK')
