"""Identity comparisons retire temporary owners without consuming real aliases."""
import gc
import sys
import weakref

events = []


class Item:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        events.append(self.name)


# Keep this original module-level construction and post-deletion observation:
# an unrelated external owner releases the object after the weakref result dies.
item = Item('external')
holder = [item]
reference = weakref.ref(item)
del item
assert reference() is not None
holder.clear()
assert reference() is None and events == ['external'], events
print('module weakref observation does not retain the external owner: OK')


def make(name):
    return Item(name)


events.clear()
decision = make('straight') is not None
assert decision and events == ['straight'], events
if make('taken') is None:
    raise AssertionError('wrong identity result')
assert events == ['straight', 'taken'], events
if make('untaken') is not None:
    assert events == ['straight', 'taken', 'untaken'], events
assert make('left') is not make('right')
assert events[-2:] == ['right', 'left'], events
print('straight and fused identity comparisons preserve finalizer order: OK')


def keep_aliases():
    item = Item('local alias')
    reference = weakref.ref(item)
    alias = item
    assert reference() is not None and item is alias
    del item
    assert reference() is alias and events == []
    del alias
    assert reference() is None and events == ['local alias'], events


events.clear()
keep_aliases()
events.clear()
for number in range(4):
    item = Item(number)
    holder = [item]
    reference = weakref.ref(item)
    del item
    assert reference() is not None
    holder.clear()
    assert reference() is None
    assert events == list(range(number + 1)), events
print('live local aliases and recreated loop results retain correct ownership: OK')

events.clear()
resurrected = []


class Reentrant:
    def __del__(self):
        gc.collect()
        resurrected.append(self)
        events.append('resurrected')


assert Reentrant() is not None
assert len(resurrected) == 1 and events == ['resurrected'], events
resurrected.clear()
gc.collect()
assert events == ['resurrected'], events
print('temporary finalizers can reenter collection and resurrect their object: OK')

active = []
unraisable = []


class Failing:
    def __del__(self):
        active.append(type(sys.exception()).__name__)
        raise LookupError('identity finalizer')


original_hook = sys.unraisablehook
sys.unraisablehook = lambda event: unraisable.append(type(event.exc_value).__name__)
try:
    try:
        raise ValueError('outer exception')
    except ValueError as outer:
        decision = Failing() is None
        assert not decision and sys.exception() is outer
        assert active == ['ValueError'] and unraisable == ['LookupError'], (active, unraisable)
finally:
    sys.unraisablehook = original_hook
print('finalizer failures preserve the handled exception and unraisable hook: OK')


def observed():
    assert make('observed') is not None
    assert events == ['observed'], events


def trace(frame, event, argument):
    return trace


for install in (sys.settrace, sys.setprofile):
    events.clear()
    install(trace)
    try:
        observed()
    finally:
        install(None)
print('trace and profile retain identity temporary lifetime semantics: OK')

monitoring = sys.monitoring
watch = None
seen = []
none_alias = None
control = True


class Enable:
    def __del__(self):
        monitoring.set_local_events(
            3, watch,
            monitoring.events.BRANCH_LEFT | monitoring.events.BRANCH_RIGHT | monitoring.events.PY_RETURN,
        )


def none_taken():
    if Enable() is None:
        return 0
    return 1


def none_fallthrough():
    if Enable() is not None:
        return 1
    return 0


def reversed_none_taken():
    if None is Enable():
        return 0
    return 1


def general_taken():
    if Enable() is object():
        return 0
    return 1


def general_fallthrough():
    if Enable() is not object():
        return 1
    return 0


def alias_none_taken():
    if Enable() is none_alias:
        return 0
    return 1


def none_next_branch():
    if Enable() is None:
        return 0
    if control:
        return 1
    return 2


def resume():
    return None


def none_call_then_branch():
    if Enable() is None:
        return 0
    resume()
    if control:
        return 1
    return 2


def not_is_none():
    if not (Enable() is None):
        return 1
    return 0


def not_is_not_none():
    if not (Enable() is not None):
        return 0
    return 1


def double_not_is_none():
    if not not (Enable() is None):
        return 0
    return 1


def double_not_is_not_none():
    if not not (Enable() is not None):
        return 1
    return 0

def multiline_none_taken():
    if (
        Enable() is None
    ):
        return 0
    return 1


def multiline_none_fallthrough():
    if (
        Enable() is not None
    ):
        return 1
    return 0


def multiline_not_is_not_none():
    if not (
        Enable() is not None
    ):
        return 0
    return 1


def multiline_double_not_is_none():
    if not not (
        Enable() is None
    ):
        return 0
    return 1


def value_probe():
    decision = Enable() is not None
    return decision

def left(code, source, destination):
    seen.append('left')


def right(code, source, destination):
    seen.append('right')


def finish(code, offset, value):
    seen.append('return')


# Pin CPython 3.14.7's source shape and entry-direction policy exactly.
return_only = ['return']
left_return = ['left', 'return']
right_return = ['right', 'return']
right_left_return = ['right', 'left', 'return']
expected_events = {
    'none_taken': (return_only, return_only, right_return),
    'none_fallthrough': (left_return, left_return, left_return),
    'reversed_none_taken': (right_return, right_return, right_return),
    'general_taken': (right_return, right_return, right_return),
    'general_fallthrough': (left_return, left_return, left_return),
    'alias_none_taken': (right_return, right_return, right_return),
    'none_next_branch': (left_return, left_return, right_left_return),
    'none_call_then_branch': (left_return, left_return, right_left_return),
    'not_is_none': (left_return, left_return, left_return),
    'not_is_not_none': (return_only, return_only, right_return),
    'double_not_is_none': (return_only, return_only, right_return),
    'double_not_is_not_none': (left_return, left_return, left_return),
    'multiline_none_taken': (return_only, return_only, right_return),
    'multiline_none_fallthrough': (left_return, left_return, left_return),
    'multiline_not_is_not_none': (return_only, return_only, right_return),
    'multiline_double_not_is_none': (return_only, return_only, right_return),
}

monitoring.use_tool_id(3, 'identity-last-use-phases')
try:
    monitoring.register_callback(3, monitoring.events.BRANCH_LEFT, left)
    monitoring.register_callback(3, monitoring.events.BRANCH_RIGHT, right)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, finish)
    for function in (
        none_taken, none_fallthrough, reversed_none_taken,
        general_taken, general_fallthrough, alias_none_taken,
        none_next_branch, none_call_then_branch, not_is_none, not_is_not_none, double_not_is_none, double_not_is_not_none,
        multiline_none_taken, multiline_none_fallthrough, multiline_not_is_not_none, multiline_double_not_is_none,
    ):
        watch = function.__code__
        for label, entry in (
            ('off', 0), ('left', monitoring.events.BRANCH_LEFT), ('right', monitoring.events.BRANCH_RIGHT),
        ):
            seen.clear()
            monitoring.set_local_events(3, watch, entry)
            result = function()
            monitoring.set_local_events(3, watch, 0)
            expected = expected_events[function.__name__][('off', 'left', 'right').index(label)]
            assert result == 1 and seen == expected, (function.__name__, label, result, list(seen), expected)
    watch = value_probe.__code__
    seen.clear()
    assert value_probe() is True
    assert seen == ['return'], seen
    monitoring.set_local_events(3, watch, 0)
finally:
    monitoring.register_callback(3, monitoring.events.BRANCH_LEFT, None)
    monitoring.register_callback(3, monitoring.events.BRANCH_RIGHT, None)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, None)
    monitoring.free_tool_id(3)

print('identity finalizers preserve CPython monitoring phases: OK')
