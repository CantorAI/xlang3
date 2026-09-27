from enum import Enum


class ListEnum(list[int], Enum):
    first = [1]


print(ListEnum.__bases__[0] is list, ListEnum.__orig_bases__[0] == list[int], ListEnum.first.value)

calls = []
prepared = []


class Base:
    pass


class Replacement:
    def __mro_entries__(self, bases):
        calls.append(len(bases))
        return (Base,)


class Meta(type):
    @classmethod
    def __prepare__(mcls, name, bases):
        prepared.append(bases[0] is Base)
        return {}


class Derived(Replacement(), metaclass=Meta):
    pass


print(calls, prepared, Derived.__bases__[0] is Base, isinstance(Derived.__orig_bases__[0], Replacement))
