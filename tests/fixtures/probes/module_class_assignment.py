import types
import module_class_target as target


class First(types.ModuleType):
    @property
    def computed(self):
        return self.value + 1

    def __getattr__(self, name):
        if name == "fallback":
            return 73
        raise AttributeError(name)

    def __call__(self, increment=0):
        return self.value + increment


class Second(types.ModuleType):
    @property
    def computed(self):
        return self.value + 2


def computed(module):
    return module.computed


namespace = target.__dict__
namespace["__call__"] = lambda: -1
assert not callable(target)
holder = types.ModuleType("holder")
holder.callback = target
for index in range(20):
    target.__class__ = First
    assert type(target) is First
    assert target.__class__ is First
    assert isinstance(target, types.ModuleType)
    assert target.__dict__ is namespace
    assert "__class__" not in namespace
    assert target.value == 42
    assert target.fallback == 73
    assert computed(target) == 43
    assert callable(target)
    assert target(5) == 47
    assert target(increment=7) == 49
    assert holder.callback(5) == 47
    assert holder.callback(increment=7) == 49
    setattr(target, "__class__", Second)
    assert type(target) is Second
    assert computed(target) == 44
    assert not callable(target)

for replacement in (None, 1, str, object):
    try:
        target.__class__ = replacement
    except TypeError:
        pass
    else:
        raise AssertionError("invalid compiled module class assignment accepted")
    try:
        setattr(target, "__class__", replacement)
    except TypeError:
        pass
    else:
        raise AssertionError("invalid setattr module class assignment accepted")
    assert type(target) is Second


class Slotted(types.ModuleType):
    __slots__ = ("extra",)


try:
    target.__class__ = Slotted
except TypeError:
    pass
else:
    raise AssertionError("different module instance layout accepted")

target.__class__ = types.ModuleType
assert type(target) is types.ModuleType
assert target.value == 42
assert not callable(target)
assert not hasattr(target, "fallback")
assert target.__dict__ is namespace
print("module-class-assignment-ok")
