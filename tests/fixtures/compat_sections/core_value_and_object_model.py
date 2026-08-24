# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Type objects, object root, and direct scalar identity policy.
class Base:
    pass

class Child(Base):
    "child-doc"
    pass

obj = Child()
same = obj
other = Child()
print(type(obj) is Child, type(Child) is type, type(type) is type)
print(Child.__doc__)
print(isinstance(obj, Child), isinstance(obj, Base), isinstance(obj, object))
print(issubclass(Child, Base), issubclass(Child, object))
print(obj is same, obj is other, None is None, True is True, 1000 is 1000)
print(id(obj) == id(same), id(obj) == id(other))

# MRO and dynamic class creation through type(name, bases, namespace).
Dynamic = type("Dynamic", (Child,), {"kind": "dynamic"})
dynamic = Dynamic()
print(Dynamic.__name__, Dynamic.__base__.__name__, Dynamic.__mro__[0].__name__, Dynamic.__mro__[-1].__name__)
print(Dynamic.kind, isinstance(dynamic, Dynamic), isinstance(dynamic, Base))

# Metaclass selection is preserved for classes and dynamic type construction.
def meta_factory(name, bases, namespace):
    namespace["from_meta"] = name + "-meta"
    return type(name, bases, namespace)

class Made(metaclass=meta_factory):
    pass

class CustomMeta(type):
    def __call__(cls, value=0):
        item = object.__new__(cls)
        item.value = value + 1
        return item

class CustomMade(metaclass=CustomMeta):
    pass

class InitMeta(type):
    def __init__(cls, name, bases, namespace):
        cls.ready = name + ":" + namespace["kind"]

class InitMade(metaclass=InitMeta):
    kind = "ok"

class NewMeta(type):
    def __new__(mcls, name, bases, namespace):
        namespace["from_new"] = name + ":new"
        return type.__new__(mcls, name, bases, namespace)

class NewMade(metaclass=NewMeta):
    pass

class PrepareMeta(type):
    @classmethod
    def __prepare__(mcls, name, bases):
        return {"prepared": name + ":prepared"}

    def __init__(cls, name, bases, namespace):
        cls.prepare_seen = namespace["prepared"]

class PrepareMade(metaclass=PrepareMeta):
    body_attr = "body"

print(Made.from_meta, type(Made).__name__)
print(type(CustomMade).__name__, CustomMade.__class__.__name__)
print(CustomMade(4).value)
print(InitMade.ready)
print(NewMade.from_new, type(NewMade).__name__)
print(PrepareMade.prepared, PrepareMade.prepare_seen, PrepareMade.body_attr)

# Descriptor lookup, property get/set/delete, and instance fallback.
class Descriptor:
    def __get__(self, obj, owner):
        if obj is None:
            return "class:" + owner.__name__
        return obj.storage + 1

    def __set__(self, obj, value):
        obj.storage = value * 2

    def __delete__(self, obj):
        obj.storage = 0

class Box:
    value = Descriptor()

    def __init__(self):
        self.storage = 3

box = Box()
print(box.value, Box.value)
box.value = 5
print(box.storage, box.value)
del box.value
print(box.storage)

class PropBox:
    def __init__(self):
        self.raw = 4

    @property
    def value(self):
        return self.raw

    @value.setter
    def value(self, v):
        self.raw = v + 1

    @value.deleter
    def value(self):
        self.raw = 0

prop = PropBox()
print(prop.value)
prop.value = 9
print(prop.raw, prop.value)
del prop.value
print(prop.raw)

# Slots restrict dynamic attributes unless __dict__ is requested.
class Slotted:
    __slots__ = ("x", "y")

    def __init__(self):
        self.x = 1
        self.y = 2

slotted = Slotted()
print(slotted.x + slotted.y)
print(Slotted.x.__name__, "member" in str(Slotted.x))
print(object.__getattribute__(slotted, "x"))
Slotted.x.__set__(slotted, 7)
print(slotted.x, Slotted.x.__get__(slotted, Slotted))
Slotted.x.__delete__(slotted)
try:
    print(slotted.x)
except Exception:
    print("slot-deleted")
try:
    slotted.z = 3
except Exception:
    print("slot-blocked")

class SlottedOpen:
    __slots__ = ("x", "__dict__")

open_slot = SlottedOpen()
open_slot.x = 5
open_slot.extra = 6
print(open_slot.x, open_slot.extra)
