from typing import Dict, Generic, TypeVar

T = TypeVar("T")


class Box(Generic[T]):
    pass


print(Box.__parameters__ == (T,))
print(Box[int].__args__ == (int,))


class MappingBox(Dict[str, T], Generic[T]):
    pass


print(tuple(base.__name__ for base in MappingBox.__bases__))
print(tuple(base.__name__ for base in MappingBox.__mro__))
