from typing import Generic, TypeVar

T = TypeVar("T")


class Parent(Generic[T]):
    pass


class Child(Generic[T], Parent[T]):
    pass


print(tuple(base.__name__ for base in Child.__bases__))
print(tuple(base.__name__ for base in Child.__mro__))
print(tuple(getattr(base, "__origin__", base).__name__ for base in Child.__orig_bases__))
