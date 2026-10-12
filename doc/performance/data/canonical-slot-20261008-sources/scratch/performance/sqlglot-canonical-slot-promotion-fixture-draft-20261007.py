"""Proposed correctness fixture; not registered or executed by its author."""


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

# Duplicate/shadowed slot declarations and wrong-owner descriptor assignments
# need a separate baseline audit before becoming assertions in a core fixture:
# current class_set_base deduplicates slot names, unlike CPython's independent
# Base.x and Child.x storage. The proposed eligibility explicitly declines the
# duplicate declaration case and all mismatched descriptor aliases.
