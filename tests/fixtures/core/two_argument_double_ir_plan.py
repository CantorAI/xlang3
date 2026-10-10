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

    def combine_with_extra_local(self, rhs):
        padding = None
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


class DerivedTriple(Triple):
    pass


derived = DerivedTriple(4.0, 5.0, 6.0)
assert first.combine(derived) == 32.0
assert derived.combine(first) == 32.0
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

# Fresh objects keep the native attribute layout eligible before observers.
# The earlier second.__dict__ fallback must not mask missing observer guards.
observer_left = Triple(1.0, 2.0, 3.0)
observer_right = Triple(4.0, 5.0, 6.0)
observed = []


def profile(frame, event, arg):
    if event == 'call':
        observed.append(frame.f_code.co_name)


sys.setprofile(profile)
try:
    assert observer_left.combine(observer_right) == 32.0
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
    assert observer_left.combine(observer_right) == 32.0
finally:
    sys.settrace(None)
assert 'combine' in observed and 'ready' in observed
print('PASS profile and trace original frames')

# Compare relative failure boundaries within EACH runtime. The extra-local
# method has the same lookup/arithmetic body but is outside this plan family.
# No CPython/XLang depth is hardcoded, and this is not a canonical-depth fix.
recursion_left = Triple(1.0, 2.0, 3.0)
recursion_right = Triple(4.0, 5.0, 6.0)


def descend(depth, left, right, blocked):
    if depth:
        return descend(depth - 1, left, right, blocked)
    if blocked:
        return left.combine_with_extra_local(right)
    return left.combine(right)


def first_recursion_failure(blocked, bound):
    for depth in range(bound + 4):
        try:
            value = descend(depth, recursion_left, recursion_right, blocked)
        except RecursionError:
            return depth
        assert value == 32.0
    raise AssertionError('No finite recursion failure observed')


previous_limit = sys.getrecursionlimit()
try:
    relative_limit = min(previous_limit, 96)
    sys.setrecursionlimit(relative_limit)
    assert first_recursion_failure(False, relative_limit) == first_recursion_failure(True, relative_limit)
finally:
    sys.setrecursionlimit(previous_limit)
print('PASS relative recursion boundary preserves fallback')
