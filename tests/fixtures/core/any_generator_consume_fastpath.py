events = []


def plain_values():
    yield 0
    yield ""
    yield 7
    yield 9


values = plain_values()
assert any(values) is True
assert next(values) == 9


def all_false():
    yield 0
    yield False
    yield None


assert any(all_false()) is False


def partly_consumed():
    yield 0
    yield 8
    yield 10


values = partly_consumed()
assert next(values) == 0
assert any(values) is True
assert next(values) == 10


def protected_values():
    try:
        yield 0
        yield 4
    finally:
        events.append("closed")


values = protected_values()
assert any(values) is True
assert events == []
try:
    next(values)
except StopIteration:
    pass
else:
    assert False
assert events == ["closed"]

print("any generator consume fast path ok")
