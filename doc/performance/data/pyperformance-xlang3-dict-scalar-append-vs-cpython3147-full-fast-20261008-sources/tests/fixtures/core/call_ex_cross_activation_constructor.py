"""Returned CallEx wrappers must preserve dynamic dispatch and object lifetime.

The C++ companion counts actual bound initializer allocations. This fixture
checks Python semantics; it does not claim a timing score or fix the separate
custom argument-expansion ordering issue.
"""

import gc
import sys
import weakref


class Base:
    def __init__(self, value=0):
        self.value = value


class Own(Base):
    def __init__(self, value=0):
        self.value = value


class Child(Base):
    pass


class Meta(type):
    pass


class MetaChild(Base, metaclass=Meta):
    pass


def construct(cls, value):
    # Exactly one expanded class call, followed by a returned wrapper frame.
    return cls(*(), **{"value": value})


def driver(cls, action=None):
    total = 0
    for value in range(32):
        if value == 16 and action is not None:
            action()
        item = construct(cls, value)
        total += item.value
    return total


for cls in (Own, Child, MetaChild):
    assert driver(cls) == 496
print("returned wrappers")


original_init = Base.__init__


def replacement(self, value=0):
    self.value = value + 100


def replace_base():
    Base.__init__ = replacement


assert driver(Child, replace_base) == 2096
Base.__init__ = original_init


class Override(Base):
    def __init__(self, value=0):
        self.value = value + 10


def delete_override():
    del Override.__init__


assert driver(Override, delete_override) == 656


def rename_base():
    Base.__name__ = "RenamedBase"


assert driver(Child, rename_base) == 496
assert Base.__name__ == "RenamedBase"
Base.__name__ = "Base"
print("initializer and name mutation")


class OtherBase:
    def __init__(self, value=0):
        self.value = value + 100


class Moving(Base):
    pass


def move_base():
    Moving.__bases__ = (OtherBase,)


assert driver(Moving, move_base) == 2096


def meta_call(cls, *args, **kwargs):
    return Own(value=kwargs["value"] + 1000)


def replace_meta_call():
    Meta.__call__ = meta_call


assert driver(MetaChild, replace_meta_call) == 16496
del Meta.__call__
assert driver(MetaChild) == 496
print("bases and metaclass mutation")


class CodeBase:
    def __init__(self, value=0):
        self.value = value


class CodeChild(CodeBase):
    pass


def replace_code():
    CodeBase.__init__.__code__ = replacement.__code__


assert driver(CodeChild, replace_code) == 2096


def construct_default(cls):
    return cls(*(), **{})


CodeBase.__init__.__defaults__ = (7,)
assert construct_default(CodeChild).value == 107
CodeBase.__init__.__defaults__ = (9,)
assert construct_default(CodeChild).value == 109


class KeywordBase:
    def __init__(self, *, value=2):
        self.value = value


class KeywordChild(KeywordBase):
    pass


assert construct_default(KeywordChild).value == 2
KeywordBase.__init__.__kwdefaults__ = {"value": 11}
assert construct_default(KeywordChild).value == 11
print("code and live defaults")


def lifetime(raises):
    def initializer(self, value=0):
        self.value = value
        if raises:
            raise RuntimeError("initializer marker")

    class Ephemeral:
        pass

    Ephemeral.__init__ = initializer
    class_ref = weakref.ref(Ephemeral)
    function_ref = weakref.ref(initializer)
    for value in range(3):
        try:
            construct(Ephemeral, value)
        except RuntimeError as error:
            assert str(error) == "initializer marker"
    del Ephemeral
    del initializer
    gc.collect()
    assert class_ref() is None
    assert function_ref() is None


lifetime(False)
try:
    raise LookupError("outer handler")
except LookupError:
    lifetime(True)
print("no retained class or initializer")


class Required:
    def __init__(self, value, *, required):
        self.value = value


for value in range(3):
    try:
        construct(Required, value)
    except TypeError as error:
        assert "required" in str(error)
    else:
        raise AssertionError("missing keyword-only argument must fail")
marker = LookupError("original init failure")


class Raising:
    def __init__(self, value=0):
        raise marker


for value in range(3):
    try:
        construct(Raising, value)
    except LookupError as error:
        assert error is marker
        names = []
        tb = error.__traceback__
        while tb is not None:
            names.append(tb.tb_frame.f_code.co_name)
            tb = tb.tb_next
        assert "construct" in names and "__init__" in names
    else:
        raise AssertionError("initializer exception must propagate")
print("binding and original exceptions")


events = []


def profile(frame, event, arg):
    if frame.f_code.co_name == "construct" and event in ("call", "return"):
        events.append(event)


sys.setprofile(profile)
try:
    assert driver(Child) == 496
finally:
    sys.setprofile(None)
assert events == [event for _ in range(32) for event in ("call", "return")]


class TraceBase:
    def __init__(self, value=0):
        self.value = value


class TraceChild(TraceBase):
    pass


changed = False


def mutate_on_call(frame, event, arg):
    global changed
    if event == "call" and frame.f_code.co_name == "__init__" and not changed:
        changed = True
        TraceBase.__init__.__code__ = replacement.__code__


sys.setprofile(mutate_on_call)
try:
    values = [construct(TraceChild, value).value for value in range(32)]
finally:
    sys.setprofile(None)
assert changed and values == [0] + [value + 100 for value in range(1, 32)]
print("profile and reentrant code mutation")
