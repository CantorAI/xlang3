"""Strict expanded-construction semantics; author has not executed this fixture."""
import gc
import sys
import weakref


def run_changes(cls, changes=None, count=12):
    values = []
    changes = changes or {}
    for index in range(count):
        if index in changes:
            changes[index]()
        obj = cls(*(), **{'x': index, 'flag': 'keyword'})
        values.append((obj.tag, obj.x, obj.flag))
        assert type(obj) is cls
    return values


class Base:
    def __init__(self, x=7, *, flag='default'):
        self.tag = 'base'
        self.x = x
        self.flag = flag


class Own(Base):
    def __init__(self, x=7, *, flag='default'):
        self.tag = 'own'
        self.x = x
        self.flag = flag


class Inherited(Base):
    pass


for cls, tag in ((Own, 'own'), (Inherited, 'base')):
    assert run_changes(cls) == [(tag, i, 'keyword') for i in range(12)]
assert Inherited(*(), **{}).x == 7
assert Inherited(*(), **{}).flag == 'default'
print('own and inherited expanded initializer binding')


class Left(Base):
    pass


class Right:
    def __init__(self, x=7, *, flag='default'):
        self.tag = 'right'
        self.x = x
        self.flag = flag


class Multi(Left, Right):
    pass


def reverse_bases():
    Multi.__bases__ = (Right, Left)


assert run_changes(Multi, {5: reverse_bases}) == [
    ('base' if i < 5 else 'right', i, 'keyword') for i in range(12)]
print('multiple inheritance and live base rebinding')


def replacement(self, x=7, *, flag='default'):
    self.tag = 'replaced'
    self.x = x
    self.flag = flag


def replace_base():
    Base.__init__ = replacement


def replace_child():
    Inherited.__init__ = Own.__init__


def delete_child():
    del Inherited.__init__


assert run_changes(Inherited, {3: replace_base, 6: replace_child, 9: delete_child}) == [
    ('base' if i < 3 else 'replaced' if i < 6 or i >= 9 else 'own', i, 'keyword')
    for i in range(12)]
print('base subclass replacement and deletion invalidate inherited caches')


class CodeBase:
    def __init__(self, x=7, *, flag='default'):
        self.tag = 'old-code'
        self.x = x
        self.flag = flag


class CodeChild(CodeBase):
    pass


def new_code(self, x=7, *, flag='default'):
    self.tag = 'new-code'
    self.x = x + 20
    self.flag = flag


def change_code():
    CodeBase.__init__.__code__ = new_code.__code__


assert run_changes(CodeChild, {4: change_code}) == [
    ('old-code' if i < 4 else 'new-code', i if i < 4 else i + 20, 'keyword')
    for i in range(12)]
def rename_code_base():
    CodeBase.__name__ = 'RenamedCodeBase'

assert run_changes(CodeChild, {4: rename_code_base}) == [
    ('new-code', i + 20, 'keyword') for i in range(12)]
assert CodeBase.__name__ == 'RenamedCodeBase'
print('initializer code mutation retains live binding')


sentinel = object()
new_calls = []


class NewBase:
    def __init__(self, x=0, *, flag='default'):
        self.x = x
        self.flag = flag


class NewChild(NewBase):
    pass


def foreign_new(cls, *args, **kwargs):
    new_calls.append(kwargs['x'])
    return sentinel


for index in range(10):
    if index == 4:
        NewBase.__new__ = staticmethod(foreign_new)
    made = NewChild(*(), **{'x': index, 'flag': 'keyword'})
    if index < 4:
        assert type(made) is NewChild and made.x == index
    else:
        assert made is sentinel
assert new_calls == list(range(4, 10))
print('new override after warmup preserves foreign result')


meta_calls = []


class DefaultMeta(type):
    pass


class MetaChild(Base, metaclass=DefaultMeta):
    pass


def custom_meta_call(cls, *args, **kwargs):
    meta_calls.append(kwargs['x'])
    return type.__call__(cls, *args, **kwargs)


def change_meta():
    DefaultMeta.__call__ = custom_meta_call


assert run_changes(MetaChild, {4: change_meta}) == [
    ('replaced', i, 'keyword') for i in range(12)]
assert meta_calls == list(range(4, 12))
print('default metaclass and live call override')


class ConstructorFailure(Exception):
    pass


last_failure = None
cause = ValueError('constructor cause')


class FailingBase:
    def __init__(self, x=0):
        global last_failure
        last_failure = ConstructorFailure(x)
        raise last_failure from cause


class FailingChild(FailingBase):
    pass


def check_failures(cls):
    for index in range(9):
        try:
            cls(*(), **{'x': index})
        except ConstructorFailure as caught:
            assert caught is last_failure and caught.args == (index,)
            assert caught.__cause__ is cause and caught.__suppress_context__
            assert sys.exception() is caught
            names = []
            tb = caught.__traceback__
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                tb = tb.tb_next
            assert names[-2:] == ['check_failures', '__init__']
        else:
            raise AssertionError('initializer exception was swallowed')
        assert sys.exception() is None


check_failures(FailingChild)


def failing_meta_call(cls, *args, **kwargs):
    global last_failure
    last_failure = ConstructorFailure(kwargs['x'])
    raise last_failure from cause


DefaultMeta.__call__ = failing_meta_call
for index in range(6):
    try:
        MetaChild(*(), **{'x': index})
    except ConstructorFailure as caught:
        assert caught is last_failure and caught.args == (index,)
        assert caught.__cause__ is cause
        names = []
        tb = caught.__traceback__
        while tb is not None:
            names.append(tb.tb_frame.f_code.co_name)
            tb = tb.tb_next
        assert names[-1] == 'failing_meta_call'
    else:
        raise AssertionError('metaclass exception was swallowed')
print('initializer metaclass failure identity cause and traceback')


class InitDescriptor:
    def __get__(self, instance, owner):
        def initialize(*, x=0, flag='default'):
            instance.tag = 'descriptor'
            instance.x = x
            instance.flag = flag
        return initialize


class DescriptorChild(Base):
    __init__ = InitDescriptor()


assert run_changes(DescriptorChild) == [('descriptor', i, 'keyword') for i in range(12)]

class InheritedDescriptorChild(DescriptorChild):
    pass

assert run_changes(InheritedDescriptorChild) == [('descriptor', i, 'keyword') for i in range(12)]
Base.__init__ = replacement
old_default = replacement.__kwdefaults__
try:
    replacement.__kwdefaults__ = {'flag': 'patched-default'}
    assert Inherited(*(), **{}).flag == 'patched-default'
finally:
    replacement.__kwdefaults__ = old_default
references = []
for index in range(12):
    owned = Inherited(*(), **{'x': index})
    references.append(weakref.ref(owned))
del owned
gc.collect()
assert all(reference() is None for reference in references)
print('descriptor defaults and constructed instance ownership')


starts = []
returns = []
code = replacement.__code__
tool = 4


def started(observed, offset):
    if observed is code:
        starts.append('start')


def returned(observed, offset, value):
    if observed is code:
        assert value is None
        returns.append('return')


sys.monitoring.use_tool_id(tool, 'inherited constructor fixture')
try:
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_START, started)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_RETURN, returned)
    sys.monitoring.set_local_events(tool, code,
        sys.monitoring.events.PY_START | sys.monitoring.events.PY_RETURN)
    assert run_changes(Inherited, count=8) == [('replaced', i, 'keyword') for i in range(8)]
finally:
    sys.monitoring.set_local_events(tool, code, 0)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_START, None)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_RETURN, None)
    sys.monitoring.free_tool_id(tool)
assert starts == ['start'] * 8 and returns == ['return'] * 8
print('inherited initializer monitoring remains observable')
