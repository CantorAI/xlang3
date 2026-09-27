import typing
import weakref


class Plain:
    pass


try:
    Plain[int]
except TypeError as error:
    print(type(error).__name__, str(error))


class Derived(list):
    pass


print(hasattr(list, "__class_getitem__"), list[int].__origin__ is list)
print(Derived[int].__origin__ is Derived)
print(hasattr(type, "__class_getitem__"), type[int].__origin__ is type)
print(hasattr(weakref.ref, "__class_getitem__"), weakref.ref[int].__origin__ is weakref.ref)
print(typing.Union[int, str] == int | str)


def function():
    pass


try:
    function[int]
except TypeError as error:
    print(type(error).__name__, str(error))
