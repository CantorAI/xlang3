import typing


class Box[T]:
    value: T


print(typing.Generic in Box.__mro__, type(Box.__type_params__[0]).__name__)
print(Box[int].__origin__ is Box)
