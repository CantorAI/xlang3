# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import abc
import _py_abc


print(abc.__file__.replace("\\", "/").endswith("/Lib/abc.py"))

class AbstractPair(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def first(self): pass

    @abc.abstractmethod
    def second(self): pass

try:
    AbstractPair()
except TypeError as exc:
    print("abstract-pair", "'first', 'second'" in str(exc))

class AbstractProperty(metaclass=abc.ABCMeta):
    @property
    @abc.abstractmethod
    def value(self): return 3

    @value.setter
    @abc.abstractmethod
    def value(self, new_value): pass

class Getter(AbstractProperty):
    @AbstractProperty.value.getter
    def value(self): return super().value

class Concrete(Getter):
    @Getter.value.setter
    def value(self, new_value): pass

print("property-clone", Concrete().value, Concrete.__abstractmethods__ == frozenset())

class PureBase(metaclass=_py_abc.ABCMeta):
    @abc.abstractclassmethod
    def build(cls): return cls.__name__

class PureConcrete(PureBase):
    @classmethod
    def build(cls): return super().build()

print("pure-abc", PureConcrete.build(), PureConcrete.__abstractmethods__ == frozenset())

class RegistryRoot(metaclass=abc.ABCMeta): pass
class RegistryMiddle(metaclass=abc.ABCMeta): pass
class RegistryLeaf(RegistryMiddle): pass
class Virtual: pass
RegistryRoot.register(RegistryMiddle)
RegistryLeaf.register(Virtual)
print("registry", issubclass(Virtual, RegistryRoot), isinstance(Virtual(), RegistryRoot))

keywords = {}
class ReceivesKeywords:
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__()
        keywords.update(kwargs)
class KeywordABC(ReceivesKeywords, abc.ABC, answer=42): pass
print("class-keywords", keywords == {"answer": 42})
