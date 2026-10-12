# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""CP-first semantic draft; no engine-private APIs or named-library shortcuts."""
import sys


class Triple:
    def __init__(self, a, b, c):
        self.a, self.b, self.c = a, b, c

    def ready(self):
        return self

    def combine(self, rhs):
        rhs.ready()
        return self.a * rhs.a + self.b * rhs.b + self.c * rhs.c


class Pair:
    def __init__(self, left, right):
        self.left, self.right = left, right

    def inspect(self):
        return self

    def measure(self, peer):
        peer.inspect()
        return self.left * peer.left - self.right * peer.right


first, second = Triple(1.0, 2.0, 3.0), Triple(4.0, 5.0, 6.0)
pair, peer = Pair(2.0, 3.0), Pair(7.0, 11.0)
for _ in range(32):
    assert first.combine(second) == 32.0
    assert pair.measure(peer) == -19.0
assert first.combine(first) == 14.0
assert pair.measure(peer=peer) == -19.0
assert Triple.combine(first, second) == 32.0
print('PASS distinct method names and argument routes')

events = []
original_ready = Triple.ready


def changed_ready(self):
    events.append('guard')
    self.a = 8.0
    return self


Triple.ready = changed_ready
assert first.combine(second) == 36.0 and events == ['guard']
Triple.ready = original_ready
second.a = 4.0
second.ready = lambda: events.append('shadow')
assert first.combine(second) == 32.0 and events[-1:] == ['shadow']
del second.ready
print('PASS guard mutation and instance callable shadow')


class GuardDescriptor:
    def __get__(self, instance, owner):
        events.append('descriptor')
        return lambda: events.append('called')


Triple.ready = GuardDescriptor()
assert first.combine(second) == 32.0 and events[-2:] == ['descriptor', 'called']
Triple.ready = original_ready
Triple.a = property(lambda self: (events.append('property'), 9.0)[1])
assert first.combine(second) == 109.0 and events[-2:] == ['property', 'property']
del Triple.a
print('PASS guard and field descriptors take original lookup')

original_code = Triple.ready.__code__
Triple.ready.__code__ = changed_ready.__code__
assert first.combine(second) == 36.0 and events[-1:] == ['guard']
Triple.ready.__code__ = original_code
second.a = 4.0
original_combine_code = Triple.combine.__code__


def replacement(self, rhs):
    return 503.0


Triple.combine.__code__ = replacement.__code__
assert first.combine(second) == 503.0
Triple.combine.__code__ = original_combine_code
assert first.combine(second) == 32.0
print('PASS main and nested code replacement')


class Hooked(Triple):
    def __getattribute__(self, name):
        if name == 'a':
            events.append('getattribute')
        return object.__getattribute__(self, name)


hooked = Hooked(4.0, 5.0, 6.0)
assert first.combine(hooked) == 32.0 and events[-1:] == ['getattribute']
second.a = 4
assert first.combine(second) == 32.0
second.a = 4.0
assert second.__dict__['a'] == 4.0
assert first.combine(second) == 32.0
print('PASS custom lookup mixed numbers and exposed dictionary')


class Marker(Exception):
    pass


marker = Marker('same marker')


def failing(self):
    events.append('raise')
    raise marker


Triple.ready = failing
try:
    first.combine(second)
except Marker as error:
    assert error is marker
    names = []
    tb = error.__traceback__
    while tb is not None:
        names.append(tb.tb_frame.f_code.co_name)
        tb = tb.tb_next
    assert 'combine' in names and 'failing' in names
else:
    raise AssertionError('guard exception lost')
assert events[-1:] == ['raise']
Triple.ready = original_ready
print('PASS exact exception and method traceback')

observed = []


def profile(frame, event, arg):
    if event == 'call':
        observed.append(frame.f_code.co_name)


sys.setprofile(profile)
try:
    assert first.combine(second) == 32.0
finally:
    sys.setprofile(None)
assert 'combine' in observed and 'ready' in observed
observed.clear()


def trace(frame, event, arg):
    if event == 'call':
        observed.append(frame.f_code.co_name)
    return trace


sys.settrace(trace)
try:
    assert first.combine(second) == 32.0
finally:
    sys.settrace(None)
assert 'combine' in observed and 'ready' in observed
print('PASS profile and trace original frames')
