from typing import Generic, TypeVar, get_args, get_origin


T = TypeVar('T')


class Box(Generic[T]):
    pass


alias = Box[int]
print(type(alias).__name__)
print(type(alias.__dict__).__name__)
print(get_origin(alias) is Box)
print(get_args(alias) == (int,))
print('__origin__' in alias.__dict__)
