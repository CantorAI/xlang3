"""Cache-free exact dict.get keeps generic method, lifetime and event behavior."""
import gc
import sys
import weakref


def invoke(receiver, key, default=None):
    return receiver.get(key, default)


table = {'present': 17, 3: 23}
fallback = []
for _ in range(8):
    assert invoke(table, 'present', fallback) == 17
    assert invoke(table, 3, fallback) == 23
    assert invoke(table, 'missing', fallback) is fallback
assert table.get('missing') is None
assert table.get('present') == 17
print('exact dict hits, misses and default identity: OK')


class Receiver:
    def get(self, key, default=None):
        return ('python', key, default)


receiver = Receiver()
for _ in range(8):
    assert invoke(receiver, 'present', 31) == ('python', 'present', 31)
    assert invoke(table, 'present', 31) == 17


def replacement(self, key, default=None):
    return ('replacement', key, default)


Receiver.get.__code__ = replacement.__code__
assert invoke(receiver, 'present', 31) == ('replacement', 'present', 31)
assert invoke(table, 'missing', 31) == 31


class Subdict(dict):
    def get(self, key, default=None):
        return ('subclass', key, default)


assert invoke(Subdict(present=41), 'present', 31) == ('subclass', 'present', 31)
print('mixed method site preserves Python replacement and dict overrides: OK')


class DynamicReceiver:
    def __init__(self):
        self.calls = 0

    def __getattribute__(self, name):
        if name == 'get':
            count = object.__getattribute__(self, 'calls') + 1
            object.__setattr__(self, 'calls', count)
            return lambda key, default=None: (count, key, default)
        return object.__getattribute__(self, name)


dynamic = DynamicReceiver()
for count in range(1, 5):
    assert invoke(dynamic, 'present', 43) == (count, 'present', 43)
    assert invoke(table, 'present', 43) == 17
print('dynamic method lookup remains fresh across exact dict calls: OK')


safe = {'nested': 47}
key_events = []


class StoredKey:
    def __hash__(self):
        return 101

    def __eq__(self, other):
        key_events.append('eq')
        assert invoke(safe, 'nested', 0) == 47
        return isinstance(other, QueryKey)


class QueryKey:
    def __hash__(self):
        key_events.append('hash')
        assert invoke(safe, 'nested', 0) == 47
        gc.collect()
        return 101


custom_table = {StoredKey(): 53}
assert invoke(custom_table, QueryKey(), 0) == 53
assert key_events == ['hash', 'eq']
print('generic key hashing and equality can reenter the dict shortcut: OK')


failure = LookupError('reentrant key failure')


class ErrorKey:
    def __hash__(self):
        assert invoke(safe, 'nested', 0) == 47
        raise failure


try:
    raise ValueError('outer')
except ValueError as outer:
    try:
        invoke(custom_table, ErrorKey(), 0)
    except LookupError as caught:
        assert caught is failure
    else:
        raise AssertionError('hash exception was lost')
    assert sys.exception() is outer
assert invoke(table, 'present', 0) == 17
print('generic key failure preserves identity and active exception state: OK')


lifetime_events = []


class Lifetime:
    def __del__(self):
        lifetime_events.append(invoke(safe, 'nested', 0))


class Rank:
    def __init__(self, rank):
        self.rank = rank


def cache_owner_round():
    class Provider:
        token = Lifetime()

        @classmethod
        def get(cls, left, right):
            return left.rank < right.rank

    reference = weakref.ref(Provider.token)
    receiver, left, right = Provider, Rank(1), Rank(2)
    for index in range(8):
        result = receiver.get(left, right)
        assert result == (True if index < 4 else 59)
        if index == 3:
            receiver, left, right = {'present': 59}, 'present', 0
            del Provider
    return reference


for count in range(1, 4):
    reference = cache_owner_round()
    gc.collect()
    assert reference() is None and lifetime_events == [47] * count
print('cached class method owner is released after a dict-ending activation: OK')


profile_events = []


def profile(frame, event, arg):
    if frame.f_code is invoke.__code__ and event in ('c_call', 'c_return', 'c_exception'):
        if getattr(arg, '__name__', '') == 'get':
            profile_events.append(event)


sys.setprofile(profile)
try:
    assert invoke(table, 'present', 0) == 17
    assert invoke(table, 'missing', 61) == 61
finally:
    sys.setprofile(None)
assert profile_events == ['c_call', 'c_return', 'c_call', 'c_return']
print('profiling preserves native dict call and return events: OK')


monitoring = sys.monitoring
tool = monitoring.PROFILER_ID
monitor_events = []


def monitor_call(code, offset, callable, arg0):
    if getattr(callable, '__name__', '') == 'get':
        monitor_events.append('call')


def monitor_return(code, offset, callable, arg0):
    if getattr(callable, '__name__', '') == 'get':
        monitor_events.append('return')


monitoring.use_tool_id(tool, 'dict-cache-touch')
try:
    monitoring.register_callback(tool, monitoring.events.CALL, monitor_call)
    monitoring.register_callback(tool, monitoring.events.C_RETURN, monitor_return)
    monitoring.set_local_events(tool, invoke.__code__, monitoring.events.CALL)
    assert invoke(table, 'present', 0) == 17
    assert invoke(table, 'missing', 67) == 67
finally:
    monitoring.set_local_events(tool, invoke.__code__, 0)
    monitoring.register_callback(tool, monitoring.events.CALL, None)
    monitoring.register_callback(tool, monitoring.events.C_RETURN, None)
    monitoring.free_tool_id(tool)
assert monitor_events == ['call', 'return', 'call', 'return']
print('local monitoring preserves native dict call and return events: OK')
