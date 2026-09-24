import typing


alias = typing.Callable[[], str | bytes]
union = str | bytes | alias
print(union.__args__[0] is str)
print(union.__args__[1] is bytes)
print(union.__args__[2] is alias)

extended = union | 42
print(extended.__args__[-1] == 42)

reversed_union = 42 | (str | bytes)
print(reversed_union.__args__[0] == 42)
