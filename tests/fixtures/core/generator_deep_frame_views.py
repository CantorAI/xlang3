import sys


def recurse(depth):
    if depth == 0:
        return sys._getframe(11).f_code.co_name
    return recurse(depth - 1)


def values():
    yield recurse(10)


print(next(values()))
