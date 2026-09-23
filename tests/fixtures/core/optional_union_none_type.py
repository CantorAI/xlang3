import types
from typing import get_args, get_origin


optional = int | None
args = get_args(optional)
print(get_origin(optional) is types.UnionType)
print(args[0] is int, args[1] is types.NoneType)
print(repr(optional))

expanded = str | optional
expanded_args = get_args(expanded)
print(expanded_args[0] is str, expanded_args[1] is int,
      expanded_args[2] is types.NoneType)

nested_optional = list[str] | None
nested_args = get_args(nested_optional)
print(nested_args[0] == list[str], nested_args[1] is types.NoneType)
