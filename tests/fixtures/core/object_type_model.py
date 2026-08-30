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

class Base:
    pass

class Child(Base):
    pass

class SuperBase:
    @classmethod
    def name(cls):
        return "base:" + cls.__name__

class SuperChild(SuperBase):
    @classmethod
    def name(cls):
        return super().name()

class SuperMetaBase:
    @classmethod
    def name(metacls):
        return "meta:" + metacls.__name__

class SuperMetaChild(SuperMetaBase):
    @classmethod
    def name(metacls):
        return super().name()

class ConstructingMeta(type):
    def __new__(metacls, name, bases, ns):
        ns["from_meta"] = name
        return super().__new__(metacls, name, bases, ns)

class MetaConstructed(metaclass=ConstructingMeta):
    pass

class EnumShapedMeta(type):
    def __new__(metacls, cls, bases, classdict, *, marker="ok", **kwds):
        classdict["from_enum_shape"] = cls + ":" + marker
        return super().__new__(metacls, cls, bases, classdict, **kwds)

class EnumShapedConstructed(metaclass=EnumShapedMeta):
    pass

class IterableMeta(type):
    def __iter__(cls):
        return (cls._member_map_[name] for name in cls._member_names_)

class IterableClass(metaclass=IterableMeta):
    _member_names_ = ["A", "B"]
    _member_map_ = {"A": 10, "B": 20}

class SetNameDescriptor:
    def __set_name__(self, owner, name):
        self.owner_name = owner.__name__
        self.attr_name = name

class SetNameTarget:
    tracked = SetNameDescriptor()

class PreparedNamespace(dict):
    def __init__(self):
        super().__init__()
        self.seen = []
    def __setitem__(self, key, value):
        self.seen.append(key)
        super().__setitem__(key, value)

class PreparedMeta(type):
    @classmethod
    def __prepare__(metacls, name, bases):
        return PreparedNamespace()
    def __new__(metacls, name, bases, ns):
        created = super().__new__(metacls, name, bases, dict(ns.items()))
        created.prepared_seen = ns.seen
        return created

class PreparedBase(metaclass=PreparedMeta):
    pass

class PreparedChild(PreparedBase):
    marker = "prepared"

def descriptor_function(self):
    return self.__class__.__name__

class property:
    def __init__(self, value):
        self.value = value
    def __get__(self, instance, owner=None):
        return self.value

class TextSubclass(str):
    pass

class MroLeft:
    pass

class MroRight:
    pass

class MroCombined(MroLeft, MroRight):
    pass

class MroText(str, MroLeft):
    pass

obj = object()
child = Child()
child.dynamic = "remove"

print(type(1) is int)
print(type(True) is bool, isinstance(True, int))
print(type("abc") is str)
print(type([1, 2]) is list)
print(type({"a": 1}) is dict)
print(type({1, 2}) is set)
print(type((1, 2)) is tuple)
print(type(type) is type, isinstance(type, type))
print(type(object) is type, isinstance(obj, object))
print(type(child) is Child)
print(isinstance(child, Child), isinstance(child, Base), isinstance(child, object))
print(issubclass(Child, Base), issubclass(Child, object), issubclass(bool, int))
print(isinstance(1, (str, int)))
print(type(child).__name__, Child.__base__.__name__)
print(id(child) == id(child), id(Child) == id(Child))
print(callable(object().__str__), type(object().__str__).__name__, object().__str__() == object().__repr__())
print(object().__format__("") == object().__str__())
try:
    object().__format__("x")
except TypeError as exc:
    print(type(exc).__name__, str(exc))
print(hasattr(object, "__reduce__"), hasattr(object, "__reduce_ex__"), isinstance(object().__reduce__(), tuple), isinstance(object().__reduce_ex__(4), tuple))
delattr(child, "dynamic")
print(hasattr(child, "dynamic"))
print(SuperChild.name())
print(SuperMetaChild.name())
print(MetaConstructed.from_meta)
print(EnumShapedConstructed.from_enum_shape)
a, b = IterableClass
print(a, b)
print(SetNameTarget.tracked.owner_name, SetNameTarget.tracked.attr_name)
print(PreparedChild.prepared_seen, PreparedChild.marker)
print(hasattr(descriptor_function, "__get__"), descriptor_function.__get__(child, Child)())
shadow_property = property(7)
print(type(shadow_property) is property, shadow_property.value, hasattr(shadow_property, "__get__"))
print(hasattr(None, "__new__"), None.__new__ is None.__new__, None.__new__(type(None)) is None)
text_subclass = str.__new__(TextSubclass, "value")
print("__new__" in str.__dict__, str.__new__(str, "base") == "base", isinstance(text_subclass, TextSubclass), str(text_subclass))
print(object.__init__(text_subclass, "value") is None)
print([item.__name__ for item in MroCombined.__mro__])
print([item.__name__ for item in MroText.__mro__])
print(str(123), int("42"), bool([]), bool([1]))
print(list((1, 2)), tuple([3, 4]), set([1, 1, 2]))
print(list(range(3)))
