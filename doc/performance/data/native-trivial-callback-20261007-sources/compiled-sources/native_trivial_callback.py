"""Native entry must share ordinary call eligibility and observable fallback."""
from collections import Counter
import contextvars
import inspect
import sys
import traceback

context = contextvars.Context()
token = object()


def constant(value, /):
    return 0


def identity(value, /):
    return value


assert context.run(constant, token) == 0
assert context.run(identity, token) is token
assert max([1, 2, 3], key=constant) == 1
assert max([1, 2, 3], key=identity) == 3
assert Counter()['absent'] == 0
print('native constant and argument returns preserve identity and ties: OK')


def default(value=8):
    return value


def keyword_only(value, *, flag=9):
    return flag


assert context.run(default) == 8
default.__defaults__ = (13,)
assert context.run(default) == 13
assert context.run(default, 4) == 4
assert context.run(keyword_only, 1) == 9
keyword_only.__kwdefaults__ = {'flag': 22}
assert context.run(keyword_only, 1) == 22
for function, args in ((constant, ()), (constant, (1, 2)), (keyword_only, (1, 2))):
    try:
        context.run(function, *args)
    except TypeError:
        pass
    else:
        raise AssertionError('invalid arity accepted')
print('defaults and unsupported signatures use normal argument binding: OK')


def capture():
    held = token

    def inner(value):
        return held

    return inner


def generator(value):
    yield value


async def coroutine(value):
    return value


assert context.run(capture(), 1) is token
iterator = context.run(generator, token)
assert next(iterator) is token
iterator.close()
awaitable = context.run(coroutine, token)
assert inspect.iscoroutine(awaitable)
awaitable.close()
print('captures, generators and coroutines retain normal entry: OK')


class StaticMissing(dict):
    @staticmethod
    def __missing__(key):
        return key


class ClassMissing(dict):
    @classmethod
    def __missing__(cls, key):
        return cls


class Override(Counter):
    def __missing__(self, key):
        return 17


assert StaticMissing()[token] is token
assert ClassMissing()[token] is ClassMissing
assert Override()[token] == 17
print('missing-key overrides and descriptor binding remain dynamic: OK')

original_code = constant.__code__
constant.__code__ = identity.__code__
assert max([1, 2, 3], key=constant) == 3


def raises(value):
    raise LookupError('replacement')


constant.__code__ = raises.__code__
try:
    context.run(constant, 1)
except LookupError as error:
    assert str(error) == 'replacement'
    assert traceback.extract_tb(error.__traceback__)[-1].name == 'raises'
else:
    raise AssertionError('code replacement ignored')
constant.__code__ = original_code
print('current code replacement and error traceback are preserved: OK')

try:
    raise ValueError('caller')
except ValueError as error:
    assert sys.exception() is error
    assert context.run(constant, 1) == 0
    assert sys.exception() is error
print('caller handled-exception state survives native reentry: OK')

observed = []


def hook(frame, event, arg):
    name = frame.f_code.co_name
    if name in ('constant', 'identity') and event in ('call', 'return'):
        observed.append((name, event))
    return hook


for set_hook, label in ((sys.settrace, 'trace'), (sys.setprofile, 'profile')):
    observed.clear()
    set_hook(hook)
    try:
        assert context.run(identity, token) is token
        assert max([1, 2, 3], key=constant) == 1
    finally:
        set_hook(None)
    assert observed.count(('identity', 'call')) == 1, observed
    assert observed.count(('identity', 'return')) == 1, observed
    assert observed.count(('constant', 'call')) == 3, observed
    assert observed.count(('constant', 'return')) == 3, observed
    print(label + ' observes every native Python callback: OK')

monitoring = sys.monitoring
watched = identity.__code__
monitor_events = []


def started(code, offset):
    if code is watched:
        monitor_events.append('start')


def returned(code, offset, value):
    if code is watched:
        assert value is token
        monitor_events.append('return')


monitoring.use_tool_id(3, 'native-trivial-callback')
try:
    monitoring.register_callback(3, monitoring.events.PY_START, started)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, returned)
    monitoring.set_local_events(3, watched, monitoring.events.PY_START | monitoring.events.PY_RETURN)
    assert context.run(identity, token) is token
    assert monitor_events == ['start', 'return'], monitor_events
finally:
    monitoring.set_local_events(3, watched, 0)
    monitoring.register_callback(3, monitoring.events.PY_START, None)
    monitoring.register_callback(3, monitoring.events.PY_RETURN, None)
    monitoring.free_tool_id(3)
print('local monitoring observes native callback start and return: OK')
