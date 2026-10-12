"""Canonical slot read optimization keeps descriptor and hook semantics live."""


def repeated_x(item, expected):
    # Keep one attribute instruction live long enough to exercise its cache.
    for unused in range(16):
        assert item.x is expected


class Base:
    __slots__ = ('padding', 'x')


first, second = object(), object()
base = Base()
base.padding = None
base.x = first
repeated_x(base, first)
base.x = second
repeated_x(base, second)
print('initialized slots and live writes keep returned object identity: OK')


del base.x
for unused in range(3):
    try:
        repeated_x(base, second)
    except AttributeError:
        pass
    else:
        raise AssertionError('deleted slot returned a cached value')
base.x = first
repeated_x(base, first)
print('deleted and restored slots preserve missing-attribute fallback: OK')


class Child(Base):
    __slots__ = ('extra',)


child = Child()
child.padding = None
child.x = second
child.extra = first
repeated_x(child, second)
print('unique inherited slot resolves its effective receiver index: OK')


saved_descriptor = Base.x
Base.alias = saved_descriptor


def repeated_alias(item, expected):
    for unused in range(16):
        assert item.alias is expected


repeated_alias(base, first)
repeated_alias(child, second)
base.x = second
repeated_alias(base, second)
print('aliased member descriptors keep original descriptor dispatch: OK')


replacement = object()
Base.x = property(lambda self: replacement)
repeated_x(base, replacement)
repeated_x(child, replacement)
Base.x = saved_descriptor
repeated_x(base, second)
repeated_x(child, second)
print('warmed base descriptor replacement invalidates derived reads: OK')


class OverrideDescriptor:
    def __get__(self, instance, owner):
        return replacement

    def __set__(self, instance, value):
        raise AssertionError('unexpected descriptor write')


Base.x = OverrideDescriptor()
repeated_x(base, replacement)
repeated_x(child, replacement)
Base.x = saved_descriptor
repeated_x(base, second)
print('custom data descriptors preempt warmed slot cache reads: OK')


hooked_value = object()


class Hooked(Base):
    __slots__ = ()

    def __getattribute__(self, name):
        if name == 'x':
            return hooked_value
        return object.__getattribute__(self, name)


hooked = Hooked()
hooked.x = first
repeated_x(hooked, hooked_value)
Hooked.__getattribute__ = lambda self, name: replacement
repeated_x(hooked, replacement)
del Hooked.__getattribute__
repeated_x(hooked, first)
print('custom attribute hooks remain live and prevent slot promotion: OK')


class Lazy(Base):
    __slots__ = ()

    def __getattr__(self, name):
        if name == 'x':
            self.x = first
            return first
        raise AttributeError(name)


lazy = Lazy()
repeated_x(lazy, first)
del lazy.x
repeated_x(lazy, first)
print('uninitialized inherited slot preserves lazy getattr refill: OK')


class WithDict(Base):
    __slots__ = ('__dict__',)


with_dict = WithDict()
with_dict.x = first
with_dict.__dict__['x'] = second
repeated_x(with_dict, first)
with_dict.__dict__['x'] = replacement
repeated_x(with_dict, first)
print('initialized member descriptors retain precedence over instance dict: OK')

assert getattr(base, 'x') is second
assert getattr(base, 'x', replacement) is second
del base.x
assert getattr(base, 'x', replacement) is replacement
base.x = second
repeated_x(base, second)
print('getattr with defaults retains deleted-slot and restored-slot behavior: OK')

# These duplicate declarations deliberately use only their visible descriptor
# storage. The CPP eligibility checks assert generic fallback; this differential
# fixture does not claim independent Base.x/Child.x duplicate-layout support.
slot_history = ['x']
HistoryBase = type('HistoryBase', (), {'__slots__': slot_history})
HistoryChild = type('HistoryChild', (HistoryBase,), {'__slots__': ()})
history = HistoryChild()
history.x = first
repeated_x(history, first)
slot_history[:] = []
del HistoryBase.__slots__
repeated_x(history, first)
history.x = second
repeated_x(history, second)
print('original slot declaration list mutation and deletion preserve live storage: OK')

HiddenDuplicate = type('HiddenDuplicate', (HistoryBase,), {'__slots__': ('x',)})
hidden_duplicate = HiddenDuplicate()
hidden_duplicate.x = first
repeated_x(hidden_duplicate, first)
hidden_duplicate.x = second
repeated_x(hidden_duplicate, second)
RepeatedOwn = type('RepeatedOwn', (), {'__slots__': ('x', 'x')})
RepeatedChild = type('RepeatedChild', (RepeatedOwn,), {'__slots__': ()})
repeated_child = RepeatedChild()
repeated_child.x = first
repeated_x(repeated_child, first)
del repeated_child.x
assert getattr(repeated_child, 'x', second) is second
repeated_child.x = second
repeated_x(repeated_child, second)
print('duplicate declarations retain visible-descriptor writes and missing fallback: OK')

class EmptyIntermediate(HistoryBase):
    __slots__ = ()

class Added(EmptyIntermediate):
    __slots__ = ('extra', '__dict__')

added = Added()
added.x = first
added.extra = second
added.__dict__['x'] = second
repeated_x(added, first)
del added.__dict__['x']
del added.x
assert getattr(added, 'x', replacement) is replacement
added.x = second
repeated_x(added, second)
print('empty intermediate and added dict layouts retain inherited descriptor precedence: OK')

class PrivateBase:
    __slots__ = ('__x',)

class PrivateChild(PrivateBase):
    __slots__ = ()

private = PrivateChild()
private._PrivateBase__x = first
for unused in range(16):
    assert private._PrivateBase__x is first
print('private slot mangling retains inherited storage identity: OK')
