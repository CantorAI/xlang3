from typing import ForwardRef, TypeVar, Union, get_args


T = TypeVar('T')
list_argument = get_args(list[T]['Main'])[0]
union_argument = get_args(Union[T, int]['Main'])[0]
direct_union_argument = get_args(Union['Main', int])[0]
print(isinstance(list_argument, ForwardRef), list_argument.__forward_arg__)
print(isinstance(union_argument, ForwardRef), union_argument.__forward_arg__)
print(isinstance(direct_union_argument, ForwardRef), direct_union_argument.__forward_arg__)
