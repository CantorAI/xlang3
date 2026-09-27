from collections.abc import Callable, Iterable


first = Callable[[Callable[[str], int]], Iterable[int]]
second = Callable[[Callable[[str], int]], Iterable[int]]
print(first == second)
print(hash(first) == hash(second))
print(len({first: 1, second: 2}))
left = int | str
right = str | int
print(left == right)
print(hash(left) == hash(right))
print(len({left: 1, right: 2}))
