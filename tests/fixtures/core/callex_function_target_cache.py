def first(left, right):
    return "first", left, right


def second(left, right):
    return "second", left, right


def invoke(function, arguments):
    return function(*arguments)


print(invoke(first, [1, 2]))
print(invoke(second, [3, 4]))
print(invoke(first, [5, 6]))


def optional(left, right=7):
    return left, right


print(invoke(optional, [8]))
optional.__defaults__ = (9,)
print(invoke(optional, [10]))
