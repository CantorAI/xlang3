"""Strict public sorted callback semantics; unchanged Python implementations.

Proposed seven-group differential fixture. Run on exact CPython 3.14.7 before
registration/application; no optimization-specific Python behavior is assumed.
"""
import gc
import sys


def order_and_identity():
    values = [(2, 'first'), (1, 'middle'), (2, 'last')]
    seen = []

    def key(value):
        seen.append(value)
        return value[0]

    result = sorted(values, key=key)
    assert result == [values[1], values[0], values[2]]
    assert all(actual is expected for actual, expected in zip(seen, values))
    assert all(actual is expected for actual, expected in zip(result, [values[1], values[0], values[2]]))
    assert sorted(values, key=key, reverse=True) == [values[0], values[2], values[1]]
    assert seen == values + values


def live_defaults_closure_and_code():
    seen = []

    def default_key(value, factor=1):
        seen.append(factor)
        default_key.__defaults__ = (-1,)
        return value * factor

    assert sorted([3, 1, 2], key=default_key) == [2, 1, 3]
    assert seen == [1, -1, -1]

    def closure_case():
        bias = 0
        seen = []

        def key(value):
            nonlocal bias
            current = value + bias
            seen.append(bias)
            bias = 10
            return current

        assert sorted([3, 1, 2], key=key) == [3, 1, 2]
        assert seen == [0, 10, 10]

    closure_case()
    code_seen.clear()
    assert sorted([3, 1, 2], key=replace_code_key) == [2, 1, 3]
    assert code_seen == [('old', 3), ('new', 1), ('new', 2)]


code_seen = []


def replacement_key(value):
    code_seen.append(('new', value))
    return -value


def replace_code_key(value):
    code_seen.append(('old', value))
    replace_code_key.__code__ = replacement_key.__code__
    return value


def live_module_globals():
    namespace = {'seen': []}
    exec("factor = 1\ndef key(value):\n    global factor\n    seen.append(factor)\n    result = factor * value\n    factor = -1\n    return result\n", namespace)
    assert sorted([3, 1, 2], key=namespace['key']) == [2, 1, 3]
    assert namespace['seen'] == [1, -1, -1]
    assert namespace['factor'] == -1


def nested_entries_and_handled_context():
    outer_error = LookupError('outer handled marker')
    seen = []

    def inner(value):
        assert sys.exception() is outer_error
        seen.append(('inner', value))
        return value

    def key(value):
        assert sys.exception() is outer_error
        seen.append(('outer', value))
        assert sorted([2, 1], key=inner) == [1, 2]
        assert sys.exception() is outer_error
        return value

    try:
        raise outer_error
    except LookupError:
        assert sorted([3, 1], key=key) == [1, 3]
        assert sys.exception() is outer_error
    assert seen == [('outer', 3), ('inner', 2), ('inner', 1), ('outer', 1), ('inner', 2), ('inner', 1)]


def original_failure_and_frames():
    marker = LookupError('key marker')
    outer = ValueError('handled outer')
    seen = []

    def key(value):
        seen.append(value)
        if value == 2:
            raise marker
        return value

    try:
        raise outer
    except ValueError:
        try:
            sorted([1, 2, 3], key=key)
        except LookupError as caught:
            assert caught is marker
            assert caught.__context__ is outer
            names = []
            traceback = caught.__traceback__
            while traceback is not None:
                names.append(traceback.tb_frame.f_code.co_name)
                traceback = traceback.tb_next
            assert 'key' in names
        else:
            raise AssertionError('sorted key failure was lost')
        assert sys.exception() is outer
    assert seen == [1, 2]


def fallbacks_and_lifetime():
    assert sorted([3, 1, 2]) == [1, 2, 3]
    assert sorted(['bbb', 'a', 'cc'], key=len) == ['a', 'cc', 'bbb']
    assert sorted([3, 1, 2], key=lambda value: 0) == [3, 1, 2]
    generator_seen = []

    def generator_key(value):
        generator_seen.append(value)
        yield value

    assert sorted([7], key=generator_key) == [7]
    assert generator_seen == []

    class Key:
        def __init__(self):
            self.seen = []

        def method(self, value):
            self.seen.append(value)
            return value

        def __call__(self, value):
            self.seen.append(value)
            return -value

    bound = Key()
    assert sorted([3, 1, 2], key=bound.method) == [1, 2, 3]
    assert bound.seen == [3, 1, 2]
    instance = Key()
    assert sorted([3, 1, 2], key=instance) == [3, 2, 1]
    assert instance.seen == [3, 1, 2]
    released = []

    class Token:
        def __del__(self):
            released.append('released')

    registry = []

    def make_key():
        token = Token()

        def key(value):
            assert token is not None
            assert not released
            registry.clear()
            gc.collect()
            assert not released
            return value

        registry.append(key)

    make_key()
    assert sorted([2, 1], key=registry[0]) == [1, 2]
    gc.collect()
    assert released == ['released']


def entry_observers():
    events = []

    def key(value):
        return value + 0

    def profile(frame, event, arg):
        if frame.f_code.co_name == 'key' and event in ('call', 'return'):
            events.append(('profile', event))

    def trace(frame, event, arg):
        if frame.f_code.co_name == 'key' and event in ('call', 'return'):
            events.append(('trace', event))
        return trace

    old_profile, old_trace = sys.getprofile(), sys.gettrace()
    try:
        sys.setprofile(profile)
        sys.settrace(trace)
        assert sorted([2, 1], key=key) == [1, 2]
    finally:
        sys.settrace(old_trace)
        sys.setprofile(old_profile)
    assert [event for observer, event in events if observer == 'profile'] == ['call', 'return', 'call', 'return']
    assert [event for observer, event in events if observer == 'trace'] == ['call', 'return', 'call', 'return']
    monitoring = sys.monitoring
    tool = 5
    monitoring_events = []

    def start(code, offset):
        monitoring_events.append('start')

    def returned(code, offset, value):
        monitoring_events.append('return')

    monitoring.use_tool_id(tool, 'scoped sorted entry fixture')
    try:
        monitoring.register_callback(tool, monitoring.events.PY_START, start)
        monitoring.register_callback(tool, monitoring.events.PY_RETURN, returned)
        monitoring.set_local_events(tool, key.__code__, monitoring.events.PY_START | monitoring.events.PY_RETURN)
        assert sorted([3, 1, 2], key=key) == [1, 2, 3]
    finally:
        monitoring.set_local_events(tool, key.__code__, 0)
        monitoring.register_callback(tool, monitoring.events.PY_START, None)
        monitoring.register_callback(tool, monitoring.events.PY_RETURN, None)
        monitoring.free_tool_id(tool)
    assert monitoring_events == ['start', 'return', 'start', 'return', 'start', 'return']


for label, case in [
    ('order-and-identity', order_and_identity),
    ('live-defaults-closure-and-code', live_defaults_closure_and_code),
    ('live-module-globals', live_module_globals),
    ('nested-entries-and-handled-context', nested_entries_and_handled_context),
    ('original-failure-and-frames', original_failure_and_frames),
    ('fallbacks-and-lifetime', fallbacks_and_lifetime),
    ('entry-observers', entry_observers),
]:
    case()
    print('PASS ' + label)
