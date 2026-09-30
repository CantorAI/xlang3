import typing


type Alias = int
left = Alias | str
right = str | Alias
print(repr(left), repr(right))
print(typing.get_args(left)[0] is Alias, typing.get_args(right)[1] is Alias)
