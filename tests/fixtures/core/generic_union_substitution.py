import copy
from typing import Awaitable, Callable, TypeVar, Union, get_args

T = TypeVar("T")
Alias = Union[list[T], Callable[..., T]]
specialized = Alias[int]
outer = get_args(specialized)
print(outer[0] == list[int])
print(get_args(outer[1])[-1] is int)
copied = copy.deepcopy(list[int])
print(copied == list[int], copied is list[int])
print(copied.__reduce__())

nested = Union[Callable[[T], T], Callable[[T], Awaitable[T]]]
print(nested.__parameters__ == (T,))
nested_int = nested[int]
nested_args = get_args(nested_int)
print(get_args(nested_args[0]) == (int, int))
print(get_args(get_args(nested_args[1])[1]) == (int,))
