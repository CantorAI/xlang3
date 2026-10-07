import sys


class Root(dict):
    def python(self, value):
        return 'root', value

    @staticmethod
    def static(value):
        return 'static', value

    @classmethod
    def class_method(cls, value):
        return cls.__name__, value

    def failure(self):
        raise ValueError('original method')


class Forward(Root):
    def native(self, key, default=None):
        return super().get(key, default)

    def python(self, value):
        return super().python(value)

    def fallback(self, value):
        return super().static(value), super().class_method(value)

    def saved(self):
        return super().python

    def expanded(self, value):
        return super().python(*(value,))

    def named(self, value):
        return super().python(value=value)

    def failure(self):
        return super().failure()


obj = Forward(x=42)
assert obj.native('x') == 42
assert obj.native('absent', 9) == 9
assert obj.python(3) == ('root', 3)
assert obj.fallback(3) == (('static', 3), ('Forward', 3))
saved = obj.saved()
assert saved.__self__ is obj
assert saved(4) == ('root', 4)
assert obj.expanded(4) == ('root', 4)
assert obj.named(5) == ('root', 5)
print('native, Python, saved and ordinary fallback methods', True)


def replacement(self, value):
    return 'changed', value


original = Root.python
try:
    Root.python = replacement
    assert obj.python(6) == ('changed', 6)
    assert saved(6) == ('root', 6)
finally:
    Root.python = original
assert obj.python(7) == ('root', 7)
print('base mutation and saved callable identity', True)


class Right(Root):
    def python(self, value):
        return 'right', value


class Diamond(Forward, Right):
    pass


assert Diamond().python(8) == ('right', 8)
print('current diamond MRO dispatch', True)

calls = []


def profile(frame, event, arg):
    if event == 'call' and frame.f_code.co_name == 'python':
        calls.append('python')
    # XLang3's existing native profile payload is the function name.
    native_name = arg if isinstance(arg, str) else getattr(arg, '__name__', None)
    if event == 'c_call' and native_name in ('get', 'dict.get'):
        calls.append('native')


sys.setprofile(profile)
try:
    assert obj.python(9) == ('root', 9)
    assert obj.native('x') == 42
finally:
    sys.setprofile(None)
assert calls.count('python') == 2
assert 'native' in calls
print('Python frames and native profile calls retained', True)

try:
    obj.failure()
except ValueError as error:
    assert str(error) == 'original method'
    names = []
    trace = error.__traceback__
    while trace is not None:
        names.append(trace.tb_frame.f_code.co_name)
        trace = trace.tb_next
    assert names.count('failure') == 2
else:
    raise AssertionError('method exception swallowed')
try:
    obj.native([])
except TypeError:
    pass
else:
    raise AssertionError('native argument error swallowed')
print('original method tracebacks and native errors retained', True)

# A native dictionary lookup can re-enter Python hashing. Keep its temporary
# receiver and already-selected callable alive even if that hook mutates a base.
class ReentrantKey:
    def __hash__(self):
        import gc
        gc.collect()
        Root.get = lambda self, key, default=None: 'new method'
        return 12345


try:
    assert Forward(x=42).native(ReentrantKey(), 9) == 9
    assert obj.native('x') == 'new method'
finally:
    del Root.get
print('temporary receiver and callable survive reentrant lookup', True)
