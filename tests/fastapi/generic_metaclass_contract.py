from typing import Generic, TypeVar

T = TypeVar("T")
observed = []


class Meta(type):
    def __new__(mcls, name, bases, namespace):
        cls = super().__new__(mcls, name, bases, namespace)
        observed.append((name, getattr(cls, "__parameters__", ()) == (T,)))
        return cls


class Base(metaclass=Meta):
    pass


class Model(Base, Generic[T]):
    pass


print(observed)
print(Model.__parameters__ == (T,))
