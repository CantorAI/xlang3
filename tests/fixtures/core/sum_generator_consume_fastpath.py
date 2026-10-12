events = []


def exact_values():
    value = yield 1
    events.append(value)
    value = yield 2
    events.append(value)
    yield 3


assert sum(exact_values()) == 6
assert events == [None, None]


def mixed_values():
    yield 1
    yield 2.5
    yield 3


assert sum(mixed_values()) == 6.5


def overflow_values():
    yield 9223372036854775807
    yield 1


assert sum(overflow_values()) == 9223372036854775808


cleanup = []


def protected_values():
    try:
        yield 4
        yield 5
    finally:
        cleanup.append("done")


assert sum(protected_values()) == 9
assert cleanup == ["done"]


def custom_values():
    yield 1
    yield 2


assert sum(custom_values(), start=10) == 13
assert sum([1, 2, 3]) == 6
print("sum generator consume fast path ok")
