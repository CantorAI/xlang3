"""Generic native-to-Python bound calls retain ordinary Python semantics."""
import contextvars
import gc
import sys
import traceback
import weakref

context = contextvars.Context()


class Receiver:
    def __init__(self, name):
        self._name_ = name

    def __hash__(self):
        return hash(self._name_)

    def read(self, value=7):
        return self._name_, value

    def fail(self):
        raise LookupError('bound callback failure')

    def generate(self):
        yield self._name_


receiver = Receiver('SELECT')
table = {receiver: 17}
for _ in range(8):
    assert table.get(receiver) == 17 and hash(receiver) == hash('SELECT')
    assert context.run(receiver.read) == ('SELECT', 7)
assert context.run(receiver.read, 9) == ('SELECT', 9)
assert context.run(receiver.read, value=11) == ('SELECT', 11)
Receiver.read.__defaults__ = (13,)
assert context.run(receiver.read) == ('SELECT', 13)
print('zero and explicit bound calls preserve hashes, defaults and keywords: OK')


saved = receiver.read


def replacement(self, value=19):
    return self._name_ + ' replacement', value


Receiver.read.__code__ = replacement.__code__
Receiver.read.__defaults__ = (19,)
assert context.run(saved) == ('SELECT replacement', 19)
assert context.run(saved, 23) == ('SELECT replacement', 23)
print('saved bound calls observe live code and default replacement: OK')


try:
    raise KeyError('outer')
except KeyError as outer:
    try:
        context.run(receiver.fail)
    except LookupError as caught:
        assert str(caught) == 'bound callback failure'
        assert 'fail' in [frame.name for frame in traceback.extract_tb(caught.__traceback__)]
    else:
        raise AssertionError('callback failure was lost')
    assert sys.exception() is outer
iterator = context.run(receiver.generate)
assert next(iterator) == 'SELECT'
try:
    next(iterator)
except StopIteration:
    pass
else:
    raise AssertionError('generator completed incorrectly')
print('exception state, callback traceback and generator binding are preserved: OK')


def check_lifetime():
    events = []
    holder = {}

    class Temporary:
        def read(self):
            holder.clear()
            gc.collect()
            assert reference() is self
            return 29

        def __del__(self):
            events.append('released')

    temporary = Temporary()
    reference = weakref.ref(temporary)
    holder['method'] = temporary.read
    del temporary
    assert context.run(holder['method']) == 29
    return reference, events


reference, events = check_lifetime()
gc.collect()
assert reference() is None and events == ['released']
print('last callable owner can disappear during callback without losing self: OK')


trace_events = []


def trace(frame, event, arg):
    if frame.f_code.co_name == 'replacement':
        trace_events.append(event)
    return trace


sys.settrace(trace)
try:
    assert context.run(saved) == ('SELECT replacement', 19)
finally:
    sys.settrace(None)
assert trace_events.count('call') == 1 and trace_events.count('return') == 1
print('zero-argument bound callbacks keep Python trace entry and return: OK')

monitoring = sys.monitoring
tool = monitoring.PROFILER_ID
starts = []
returns = []
code = Receiver.read.__code__


def monitor_start(current_code, offset):
    starts.append(current_code)


def monitor_return(current_code, offset, value):
    returns.append(value)


monitoring.use_tool_id(tool, 'native-bound-zero-args')
try:
    monitoring.register_callback(tool, monitoring.events.PY_START, monitor_start)
    monitoring.register_callback(tool, monitoring.events.PY_RETURN, monitor_return)
    monitoring.set_local_events(tool, code, monitoring.events.PY_START | monitoring.events.PY_RETURN)
    assert context.run(saved) == ('SELECT replacement', 19)
finally:
    monitoring.set_local_events(tool, code, 0)
    monitoring.register_callback(tool, monitoring.events.PY_START, None)
    monitoring.register_callback(tool, monitoring.events.PY_RETURN, None)
    monitoring.free_tool_id(tool)
assert len(starts) == 1 and returns == [('SELECT replacement', 19)]
print('local monitoring observes ordinary native-bound Python entry and return: OK')
