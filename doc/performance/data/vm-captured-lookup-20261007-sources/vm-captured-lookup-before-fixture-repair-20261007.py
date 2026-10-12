"""Ordinary call shortcuts must preserve the live Python lookup boundary."""
from collections import Counter
import sys
import traceback


key_a = (b'a', b'right')
key_b = (b'b', b'right')


def factory(mapping):
    def lookup(key):
        return mapping[key]

    def replace(value):
        nonlocal mapping
        mapping = value

    return lookup, replace


def repeated(callable, key, expected):
    # Reuse one instruction so both cold and warmed UserFunction entries run.
    for unused in range(12):
        assert callable(key) == expected


lookup, replace = factory({key_a: 11, key_b: 13})
repeated(lookup, key_a, 11)
replace({key_a: 17})
repeated(lookup, key_a, 17)
replace(Counter({key_a: 19}))
repeated(lookup, key_a, 19)
repeated(lookup, key_b, 0)
print('warm direct calls read live captured cells and Counter misses: OK')


def bound_factory(mapping):
    def lookup(self, key):
        return mapping[key]

    def replace(value):
        nonlocal mapping
        mapping = value

    return lookup, replace


class Holder:
    pass


bound_function, replace_bound = bound_factory({key_a: 23})
Holder.lookup = bound_function
holder = Holder()
saved_method = holder.lookup
repeated(saved_method, key_a, 23)
replace_bound({key_a: 29})
repeated(saved_method, key_a, 29)
print('warm bound calls preserve leading self and live closure binding: OK')


def changed_factory(mapping):
    def increment(key):
        return mapping[key] + 1
    return increment


lookup, replace = factory({key_a: 31})
original_code = lookup.__code__
repeated(lookup, key_a, 31)
lookup.__code__ = changed_factory({key_a: 0}).__code__
repeated(lookup, key_a, 32)
lookup.__code__ = original_code
replace({key_a: 37})
repeated(lookup, key_a, 37)
try:
    raise ValueError('caller')
except ValueError as error:
    repeated(lookup, key_a, 37)
    assert sys.exception() is error
print('warm code replacement and handled exception state remain visible: OK')


def default_factory(mapping):
    def lookup(key=key_a):
        return mapping[key]
    return lookup


default_lookup = default_factory({key_a: 41, key_b: 43})
repeated(default_lookup, key_a, 41)
assert default_lookup() == 41
default_lookup.__defaults__ = (key_b,)
assert default_lookup() == 43
assert default_lookup(key=key_a) == 41
assert default_lookup(*(key_b,)) == 43
assert default_lookup(**{'key': key_a}) == 41
for action in (
    lambda: default_lookup(key_a, key=key_b),
    lambda: default_lookup(other=key_a),
    lambda: default_lookup(key_a, key_b),
):
    try:
        action()
    except TypeError:
        pass
    else:
        raise AssertionError('invalid argument binding accepted')
print('default keyword expansion and argument error fallbacks retain binding: OK')


class Override(dict):
    def __getitem__(self, key):
        return 47


class Missing(dict):
    def __missing__(self, key):
        return 59


lookup, replace = factory(Override({key_a: 1}))
repeated(lookup, key_a, 47)
Override.__getitem__ = lambda self, key: 53
repeated(lookup, key_a, 53)
replace(Missing())
repeated(lookup, key_b, 59)
replace({key_a: 61})
repeated(lookup, key_a, 61)
try:
    lookup(key_b)
except KeyError as error:
    assert traceback.extract_tb(error.__traceback__)[-1].name == 'lookup'
else:
    raise AssertionError('missing key accepted')
print('overrides missing hooks and failing lookup traceback retain Python entry: OK')


events = []


class Key:
    def __hash__(self):
        events.append('hash')
        return 7

    def __eq__(self, other):
        events.append('equal')
        return isinstance(other, Key)


stored, query = Key(), Key()
lookup, replace = factory({stored: 67})
events.clear()
repeated(lookup, query, 67)
assert 'hash' in events and 'equal' in events
print('custom hash and equality callbacks are never speculated or skipped: OK')


def protocol_factory(mapping):
    def initializer(self, key):
        return mapping[key]

    def getter(self, key):
        return mapping[key]

    def setter(self, key, value):
        return mapping[key]

    def property_getter(self):
        return mapping[self]

    return initializer, getter, setter, property_getter


protocol_mapping = {key_a: None, key_b: 71}
initializer, getter, setter, property_getter = protocol_factory(protocol_mapping)


class ProtocolHolder:
    pass


ProtocolHolder.__init__ = initializer
ProtocolHolder.__getitem__ = getter
ProtocolHolder.__setitem__ = setter
ProtocolHolder.item = property(property_getter)
protocol_holder = ProtocolHolder(key_a)
assert isinstance(protocol_holder, ProtocolHolder)
assert protocol_holder[key_b] == 71
protocol_holder[key_b] = 999
assert isinstance(protocol_holder, ProtocolHolder)
assert protocol_holder[key_b] == 71
protocol_mapping[protocol_holder] = 73
assert protocol_holder.item == 73
print('constructor property and subscription contexts retain return modes: OK')


lookup, replace = factory({key_a: 79})
observed = []


def hook(frame, event, argument):
    if frame.f_code.co_name == 'lookup' and event in ('call', 'return'):
        observed.append(event)
    return hook


for setter in (sys.settrace, sys.setprofile):
    repeated(lookup, key_a, 79)
    observed.clear()
    setter(hook)
    try:
        repeated(lookup, key_a, 79)
    finally:
        setter(None)
    assert observed == ['call', 'return'] * 12, observed
print('trace and profile observe every warmed ordinary call: OK')


monitoring = sys.monitoring
monitored = []


def start(code, offset):
    monitored.append('start')


def finish(code, offset, value):
    assert value == 79
    monitored.append('return')


monitoring.use_tool_id(3, 'vm-captured-lookup')
try:
    monitoring.register_callback(3, monitoring.events.PY_START, start)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, finish)
    monitoring.set_local_events(3, lookup.__code__, monitoring.events.PY_START | monitoring.events.PY_RETURN)
    repeated(lookup, key_a, 79)
    assert monitored == ['start', 'return'] * 12, monitored
finally:
    monitoring.set_local_events(3, lookup.__code__, 0)
    monitoring.register_callback(3, monitoring.events.PY_START, None)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, None)
    monitoring.free_tool_id(3)
print('local monitoring observes warmed function entry and return: OK')


released = []
started_at = []
phase = [0]
watched_code = None


class EnableMonitoring:
    def __del__(self):
        released.append('released')
        monitoring.set_local_events(3, watched_code, monitoring.events.PY_START)


def finalizer_started(code, offset):
    started_at.append(phase[0])


def drive_finalizer(lookup, mapping):
    # One repeated CALL output can own a prior result after its dict entry is
    # replaced. Releasing that output may enable monitoring inside the hit.
    # The following call must refresh eligibility and construct its frame.
    for index in range(3):
        phase[0] = index
        if index == 2:
            assert released == ['released']
        lookup(key_a)
        if index == 0:
            mapping[key_a] = 83


finalizer_mapping = {key_a: EnableMonitoring()}
finalizer_lookup, replace = factory(finalizer_mapping)
watched_code = finalizer_lookup.__code__
monitoring.use_tool_id(3, 'vm-captured-finalizer')
try:
    monitoring.register_callback(3, monitoring.events.PY_START, finalizer_started)
    drive_finalizer(finalizer_lookup, finalizer_mapping)
    assert released == ['released']
    assert 2 in started_at, started_at
finally:
    monitoring.set_local_events(3, watched_code, 0)
    monitoring.register_callback(3, monitoring.events.PY_START, None)
    monitoring.free_tool_id(3)
print('previous output finalizers refresh monitoring before the next call: OK')
