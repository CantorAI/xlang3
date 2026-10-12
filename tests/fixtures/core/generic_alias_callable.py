from functools import partial


factory = partial(list[int], [1, 2, 3])
print(callable(list[int]), callable(int | str), factory())
