"""Exercise code writes at live cached sites, without replacing Python libraries."""


def old(a, b=7, *, flag=9):
    "original doc"
    return a + b + flag


def new(a, b=100, *, flag=200):
    return a * b + flag


defaults = old.__kwdefaults__
assert old.__code__ is old.__code__
original_name = old.__name__
original_qualname = old.__qualname__
for iteration in range(4):
    if iteration == 2:
        old.__code__ = new.__code__
    if iteration >= 2:
        assert old.__code__ is new.__code__
    assert old(2) == (18 if iteration < 2 else 23)
    assert old(*(2,), **{}) == (18 if iteration < 2 else 23)
assert old.__defaults__ == (7,)
assert old.__kwdefaults__ is defaults
assert old.__name__ == original_name
assert old.__qualname__ == original_qualname
assert old.__doc__ == "original doc"
defaults["flag"] = 3
assert old(2) == 17


def binary(a, b):
    return a + b


def subtract(a, b):
    return a - b


for iteration in range(4):
    if iteration == 2:
        binary.__code__ = subtract.__code__
    assert binary(10, 3) == (13 if iteration < 2 else 7)


def changed_signature(a, b, c):
    return a, b, c


old.__code__ = changed_signature.__code__
assert old(1, 2) == (1, 2, 7)


class Holder:
    __slots__ = ("x", "y")

    def __init__(self, value):
        self.x = value
        self.y = value + 1

    def total(self):
        return self.x + self.y

    @property
    def chosen(self):
        return self.x

    @chosen.setter
    def chosen(self, value):
        self.x = value

    @chosen.deleter
    def chosen(self):
        self.x = -1


def init_new(self, value):
    self.x = value * 2
    self.y = value * 3


def total_new(self):
    return self.x - self.y


def getter_new(self):
    return self.y


def setter_new(self, value):
    self.y = value


def deleter_new(self):
    self.y = -2


obj = Holder(5)
saved_method = obj.total
for iteration in range(4):
    if iteration == 2:
        Holder.__init__.__code__ = init_new.__code__
        Holder.total.__code__ = total_new.__code__
        Holder.chosen.fget.__code__ = getter_new.__code__
        Holder.chosen.fset.__code__ = setter_new.__code__
        Holder.chosen.fdel.__code__ = deleter_new.__code__
    constructed = Holder(5)
    assert (constructed.x, constructed.y) == ((5, 6) if iteration < 2 else (10, 15))
    assert obj.total() == (11 if iteration < 2 else -1)
    assert saved_method() == (11 if iteration < 2 else -1)
    assert obj.chosen == (5 if iteration < 2 else 6)
    obj.chosen = 20
    assert (obj.x, obj.y) == ((20, 6) if iteration < 2 else (5, 20))
    del obj.chosen
    assert (obj.x, obj.y) == ((-1, 6) if iteration < 2 else (5, -2))
    obj.x, obj.y = 5, 6


def closure(value, multiply=False):
    if multiply:
        def inner(argument):
            return value * argument
    else:
        def inner(argument):
            return value + argument
    return inner


captured = closure(10)
replacement = closure(200, True)
captured.__code__ = replacement.__code__
assert captured(3) == 30, "replacement must retain the original closure cells"

for use_setattr in (False, True):
    try:
        if use_setattr:
            setattr(captured, "__code__", 42)
        else:
            captured.__code__ = 42
    except TypeError:
        pass
    else:
        assert False, "non-code write must fail"
    try:
        if use_setattr:
            setattr(captured, "__code__", binary.__code__)
        else:
            captured.__code__ = binary.__code__
    except ValueError:
        pass
    else:
        assert False, "incompatible closure must fail"
assert captured(3) == 30

namespace = {"marker": "replacement globals"}
exec("def different_globals():\n    return marker\n", namespace)
marker = "original globals"


def keep_globals():
    return "before"


keep_globals.__code__ = namespace["different_globals"].__code__
assert keep_globals() == "original globals"


def running():
    running.__code__ = keep_globals.__code__
    return "old active frame"


assert running() == "old active frame"
assert running() == "original globals"


def generator_old():
    yield "old generator"
    yield "old continuation"


def generator_new():
    yield "new generator"


suspended = generator_old()
assert next(suspended) == "old generator"
generator_old.__code__ = generator_new.__code__
assert next(suspended) == "old continuation"
assert next(generator_old()) == "new generator"

import types

@types.coroutine
def iterable_coroutine():
    yield None

assert iterable_coroutine.__code__.co_flags & 0x100
constructed_coroutine = types.FunctionType(iterable_coroutine.__code__, globals())
assert constructed_coroutine.__code__ is iterable_coroutine.__code__
assert constructed_coroutine.__code__.co_flags & 0x100

print("function code replacement ok")
