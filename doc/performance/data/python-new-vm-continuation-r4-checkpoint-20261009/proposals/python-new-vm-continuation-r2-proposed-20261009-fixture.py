# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""CP-first draft. No runtime result or proposed transcript is yet accepted."""
import gc
import sys
import weakref


def ordinary_call(kind, *values):
    # Explicit ordinary Call sites are exercised separately below; this site
    # deliberately exercises CallEx with an exact Tuple argument bundle.
    return kind(*values)


def direct_call(kind, value):
    return kind(value)


class Basic:
    def __new__(cls, value=11):
        self = object.__new__(cls)
        self.from_new = value
        return self

    def __init__(self, value=12):
        self.from_init = value


assert direct_call(Basic, 3).__dict__ == {'from_new': 3, 'from_init': 3}
assert ordinary_call(Basic).__dict__ == {'from_new': 11, 'from_init': 12}
Basic.__new__.__defaults__ = (21,)
Basic.__init__.__defaults__ = (22,)
assert ordinary_call(Basic).__dict__ == {'from_new': 21, 'from_init': 22}
assert ordinary_call(Basic, 4).__dict__ == {'from_new': 4, 'from_init': 4}
print('PASS python-new defaults and ordinary entry')

assert Basic(*(5,)).from_init == 5
assert Basic(*(), *(6,)).from_init == 6
assert Basic(*[7]).from_init == 7
assert Basic(value=8).from_init == 8
assert Basic(**{'value': 9}).from_init == 9
expansion_events = []


class Expanded:
    def __iter__(self):
        expansion_events.append('iter')
        yield 10


assert Basic(*Expanded()).from_init == 10
assert expansion_events == ['iter']
try:
    Basic(*Expanded(), value=10)
except TypeError:
    pass
else:
    raise AssertionError('duplicate binding must fail')
assert expansion_events == ['iter', 'iter']
print('PASS python-new tuple expansion and fallbacks')

fresh_events = []


def first_init(self, value):
    fresh_events.append(('first', value))


def second_init(self, value):
    fresh_events.append(('second', value))


class Fresh:
    __init__ = first_init

    def __new__(cls, value):
        cls.__init__ = second_init
        return object.__new__(cls)


direct_call(Fresh, 1)
assert fresh_events == [('second', 1)]
del Fresh.__init__
direct_call(Fresh, 2)
assert fresh_events == [('second', 1), ('second', 2)]


def changed_new(cls, value):
    self = object.__new__(cls)
    self.changed = value
    cls.__init__ = first_init
    return self


Fresh.__new__.__code__ = changed_new.__code__
assert direct_call(Fresh, 3).changed == 3
assert fresh_events[-1] == ('first', 3)
print('PASS python-new fresh init and code replacement')

foreign_events = []
foreign_result = object()


class Foreign:
    def __new__(cls, value):
        return foreign_result

    def __init__(self, value):
        foreign_events.append(value)


assert direct_call(Foreign, 1) is foreign_result and foreign_events == []


class Base:
    def __new__(cls, value):
        return object.__new__(Derived)

    def __init__(self, value):
        raise AssertionError('returned subtype chooses its initializer')


class Derived(Base):
    def __init__(self, value):
        self.seen = value


assert type(direct_call(Base, 4)) is Derived
assert direct_call(Base, 4).seen == 4


class Before:
    __slots__ = ()

    def __init__(self, value):
        self.seen = ('before', value)


class After:
    __slots__ = ()

    def __init__(self, value):
        self.seen = ('after', value)


class ChangesMRO(Before):
    def __new__(cls, value):
        result = object.__new__(cls)
        cls.__bases__ = (After,)
        return result


assert direct_call(ChangesMRO, 5).seen == ('after', 5)
print('PASS python-new returned type and live MRO')

marker = ValueError('new continuation marker')


class RaisesNew:
    def __new__(cls, value):
        raise marker


class RaisesInit:
    def __new__(cls, value):
        return object.__new__(cls)

    def __init__(self, value):
        raise marker


for kind, expected_frame in ((RaisesNew, '__new__'), (RaisesInit, '__init__')):
    try:
        direct_call(kind, 1)
    except ValueError as error:
        assert error is marker
        names = []
        traceback = error.__traceback__
        while traceback is not None:
            names.append(traceback.tb_frame.f_code.co_name)
            traceback = traceback.tb_next
        assert expected_frame in names and 'direct_call' in names
        marker.__traceback__ = None
    else:
        raise AssertionError('exception was swallowed')
active_marker = LookupError('handled caller context')


class Context:
    def __new__(cls, value):
        assert sys.exception() is active_marker
        return object.__new__(cls)

    def __init__(self, value):
        assert sys.exception() is active_marker


try:
    raise active_marker
except LookupError:
    direct_call(Context, 1)
    assert sys.exception() is active_marker
print('PASS python-new exceptions traceback and handled context')

cleanup_events = []


def cleanup_init(self, value):
    cleanup_events.append(('init', value))


class CleanupToken:
    def __init__(self, kind):
        self.kind = kind

    def __del__(self):
        cleanup_events.append('drop')
        self.kind.__init__ = cleanup_init


def make_cleanup_token(kind):
    return CleanupToken(kind)


class Cleanup:
    def __new__(cls, value):
        token = make_cleanup_token(cls)
        return object.__new__(cls)

    def __init__(self, value):
        raise AssertionError('new frame cleanup must precede init selection')


direct_call(Cleanup, 6)
assert cleanup_events == ['drop', ('init', 6)]
print('PASS python-new frame cleanup before init selection')


def owner_case():
    class Payload:
        pass

    payload = Payload()
    payload_ref = weakref.ref(payload)

    class Ephemeral:
        def __new__(cls, value):
            result = object.__new__(cls)
            # Mutation must not destroy the active selected function/code.
            del cls.__new__
            result.value = value
            return result

        def __init__(self, value):
            assert self.value is value

    class_ref = weakref.ref(Ephemeral)
    function_ref = weakref.ref(Ephemeral.__new__)
    result = direct_call(Ephemeral, payload)
    assert result.value is payload
    return class_ref, function_ref, payload_ref


owner_refs = owner_case()
gc.collect()
assert all(ref() is None for ref in owner_refs)
print('PASS python-new temporary owners retire after construction')

profile_events = []


def profile(frame, event, arg):
    name = frame.f_code.co_name
    if event in ('call', 'return') and name in ('__new__', '__init__', 'direct_call'):
        profile_events.append((name, event))


class Observed:
    def __new__(cls, value):
        if value == 2:
            sys.setprofile(profile)
        return object.__new__(cls)

    def __init__(self, value):
        assert sys._getframe().f_back.f_code.co_name == 'direct_call'


try:
    sys.setprofile(profile)
    direct_call(Observed, 1)
finally:
    sys.setprofile(None)
assert profile_events == [('direct_call', 'call'), ('__new__', 'call'),
                          ('__new__', 'return'), ('__init__', 'call'),
                          ('__init__', 'return'), ('direct_call', 'return')]
profile_events.clear()
try:
    direct_call(Observed, 2)
finally:
    sys.setprofile(None)
assert profile_events == [('__new__', 'return'), ('__init__', 'call'),
                          ('__init__', 'return'), ('direct_call', 'return')]
trace_events = []


def trace(frame, event, arg):
    if frame.f_code.co_name == '__init__' and event in ('call', 'return'):
        trace_events.append(event)
    return trace


try:
    sys.settrace(trace)
    direct_call(Observed, 3)
finally:
    sys.settrace(None)
assert trace_events == ['call', 'return']
descriptor_events = []


class NewDescriptor:
    def __get__(self, instance, owner):
        descriptor_events.append('get')

        def selected(cls, value):
            descriptor_events.append('new')
            return object.__new__(cls)

        return selected


class Descriptor:
    __new__ = NewDescriptor()

    def __init__(self, value):
        self.value = value


assert direct_call(Descriptor, 4).value == 4
assert descriptor_events == ['get', 'new']


class GeneratorNew:
    def __new__(cls, value):
        yield value


generator = direct_call(GeneratorNew, 5)
assert next(generator) == 5
generator.close()


class AsyncNew:
    async def __new__(cls, value):
        return value


coroutine = direct_call(AsyncNew, 6)
coroutine.close()


class Meta(type):
    def __call__(cls, value):
        return ('meta', value)


class CustomMeta(metaclass=Meta):
    def __new__(cls, value):
        raise AssertionError('custom metaclass must retain control')


assert direct_call(CustomMeta, 7) == ('meta', 7)
assert direct_call(int, 8) == 8
print('PASS python-new observers and nonordinary fallbacks')
