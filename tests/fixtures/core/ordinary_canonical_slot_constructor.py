"""Own-slot constructor behavior; paired C++ counters prove actual admission.

This transcript is proposed until the exact CPython 3.14.7 reference runs.
No library code or benchmark workload is replaced by this fixture.
"""

import gc
import sys
import weakref


class Box:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


def construct(cls, value):
    return cls(value)


def driver(cls, action=None):
    total = 0
    for value in range(32):
        if value == 16 and action is not None:
            action()
        item = cls(value)
        total += item.obj
    return total


assert driver(Box) == 496
assert sum(construct(Box, value).obj for value in range(32)) == 496
assert sorted(range(31, -1, -1), key=lambda value: Box(value).obj) == list(range(32))
assert sorted(((3, 9), (1, 7), (2, 8)),
              key=lambda pair: (Box(pair[0]).obj, Box(pair[1]).obj)) == [(1, 7), (2, 8), (3, 9)]
print("ordinary calls, returned wrappers and native sorted")


class PropertyBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


property_slot = PropertyBox.obj
property_events = []


def set_property(self, value):
    property_events.append(value)
    property_slot.__set__(self, value + 100)


def replace_property():
    PropertyBox.obj = property(lambda self: property_slot.__get__(self), set_property)


assert driver(PropertyBox, replace_property) == 2096
assert property_events == list(range(16, 32))
PropertyBox.obj = property_slot
assert driver(PropertyBox) == 496
del PropertyBox.obj
try:
    construct(PropertyBox, 4)
except AttributeError:
    pass
else:
    raise AssertionError("deleted slot descriptor cannot be bypassed")
PropertyBox.obj = property_slot
assert construct(PropertyBox, 4).obj == 4
print("slot property replacement, deletion and restoration")


descriptor_events = []


class Descriptor:
    def __set__(self, instance, value):
        descriptor_events.append(value)
        property_slot.__set__(instance, value + 200)

    def __get__(self, instance, owner):
        if instance is None:
            return self
        return property_slot.__get__(instance)


PropertyBox.obj = Descriptor()
assert driver(PropertyBox) == 6896
assert descriptor_events == list(range(32))
PropertyBox.obj = property_slot


class AliasBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.alias = obj


AliasBox.alias = AliasBox.obj
assert driver(AliasBox) == 496


class ForeignBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


ForeignBox.obj = Box.obj
try:
    construct(ForeignBox, 1)
except TypeError:
    pass
else:
    raise AssertionError("foreign descriptor must retain its owner check")


class InheritedBox(Box):
    __slots__ = ()


class DuplicateBox:
    __slots__ = ("obj", "obj")

    def __init__(self, obj):
        self.obj = obj


assert driver(InheritedBox) == 496
assert driver(DuplicateBox) == 496
print("custom descriptors, alias, foreign and non-own layouts")


setter_events = []


class SetterBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj

    def __setattr__(self, name, value):
        setter_events.append((name, value))
        object.__setattr__(self, name, value + 10)


assert driver(SetterBox) == 816
assert setter_events == [("obj", value) for value in range(32)]
new_events = []


class NewBox:
    __slots__ = ("obj",)

    def __new__(cls, obj):
        new_events.append(obj)
        return object.__new__(cls)

    def __init__(self, obj):
        self.obj = obj


assert driver(NewBox) == 496
assert new_events == list(range(32))


class Meta(type):
    def __call__(cls, obj):
        return super().__call__(obj + 20)


class MetaBox(metaclass=Meta):
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


assert driver(MetaBox) == 1136
print("setattr, new and metaclass dispatch")


class CodeBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


def plus_hundred(self, obj):
    self.obj = obj + 100


def replace_code():
    CodeBox.__init__.__code__ = plus_hundred.__code__


assert driver(CodeBox, replace_code) == 2096
CodeBox.__init__.__defaults__ = (7,)
assert CodeBox().obj == 107
CodeBox.__init__.__defaults__ = (9,)
assert CodeBox().obj == 109


class KeywordBox:
    __slots__ = ("obj",)

    def __init__(self, *, obj=3):
        self.obj = obj


assert KeywordBox().obj == 3
KeywordBox.__init__.__kwdefaults__ = {"obj": 11}
assert KeywordBox().obj == 11
assert Box(obj=12).obj == 12
print("live initializer code, defaults and keyword signature")


for args in ((), (1, 2)):
    try:
        Box(*args)
    except TypeError:
        pass
    else:
        raise AssertionError("wrong positional arity must fail")
try:
    Box(1, unexpected=2)
except TypeError:
    pass
else:
    raise AssertionError("unexpected keyword must fail")
marker = LookupError("original slot initializer")


class RaisingBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        raise marker


try:
    raise RuntimeError("outer slot handler")
except RuntimeError as outer:
    for value in range(3):
        try:
            construct(RaisingBox, value)
        except LookupError as error:
            assert error is marker and error.__context__ is outer
            names = []
            tb = error.__traceback__
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                tb = tb.tb_next
            assert "construct" in names and "__init__" in names
        else:
            raise AssertionError("initializer exception must propagate")
print("binding failures and original exception context")


def lifetime():
    class Ephemeral:
        __slots__ = ("obj",)

        def __init__(self, obj):
            self.obj = obj

    class_ref = weakref.ref(Ephemeral)
    init_ref = weakref.ref(Ephemeral.__init__)
    assert driver(Ephemeral) == 496
    del Ephemeral
    gc.collect()
    assert class_ref() is None and init_ref() is None


lifetime()
retirements = []


class Payload:
    def __del__(self):
        retirements.append("payload")


item = Box(Payload())
assert retirements == []
del item
gc.collect()
assert retirements == ["payload"]
print("class, initializer and slot payload ownership")


events = []
init_code = Box.__init__.__code__


def profile(frame, event, arg):
    if frame.f_code is init_code and event in ("call", "return"):
        events.append(event)


sys.setprofile(profile)
try:
    assert construct(Box, 5).obj == 5
    assert construct(Box, 6).obj == 6
finally:
    sys.setprofile(None)
assert events == ["call", "return", "call", "return"]
events.clear()


def trace(frame, event, arg):
    if frame.f_code is init_code and event in ("call", "return"):
        events.append(event)
    return trace


sys.settrace(trace)
try:
    assert construct(Box, 7).obj == 7
finally:
    sys.settrace(None)
assert events == ["call", "return"]
print("profile and trace expose initializer frames")


monitor_events = []
tool = 4
sys.monitoring.use_tool_id(tool, "canonical-slot-fixture")


def on_start(code, offset):
    assert code is init_code
    monitor_events.append("start")


def on_return(code, offset, result):
    assert code is init_code and result is None
    monitor_events.append("return")


try:
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_START, on_start)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_RETURN, on_return)
    sys.monitoring.set_local_events(tool, init_code,
                                    sys.monitoring.events.PY_START | sys.monitoring.events.PY_RETURN)
    assert construct(Box, 8).obj == 8
    assert construct(Box, 9).obj == 9
finally:
    sys.monitoring.set_local_events(tool, init_code, 0)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_START, None)
    sys.monitoring.register_callback(tool, sys.monitoring.events.PY_RETURN, None)
    sys.monitoring.free_tool_id(tool)
assert monitor_events == ["start", "return", "start", "return"]
print("local monitoring exposes initializer entry and return")


class ReentryBox:
    __slots__ = ("obj",)

    def __init__(self, obj):
        self.obj = obj


reentry_events = []


def reentry_profile(frame, event, arg):
    if frame.f_code.co_name == "plus_hundred" and event == "call":
        reentry_events.append("call")


class OutputPayload:
    def __del__(self):
        ReentryBox.__init__ = plus_hundred
        sys.setprofile(reentry_profile)


def replace_old_output():
    item = ReentryBox(OutputPayload())
    item = ReentryBox(1)
    return item.obj, ReentryBox(2).obj


try:
    assert replace_old_output() == (1, 102)
finally:
    sys.setprofile(None)
assert reentry_events == ["call"]
print("old output finalizer changes the next dispatch and observers")
