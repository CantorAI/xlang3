import sys
outer = LookupError("outer")
seen = []
def key(value):
    assert sys.exception() is outer
    try:
        raise KeyError("inner")
    except KeyError:
        assert isinstance(sys.exception(), KeyError)
    assert sys.exception() is outer
    seen.append(value)
    return value
try:
    raise outer
except LookupError:
    assert sorted([2, 1], key=key) == [1, 2]
    assert sys.exception() is outer
assert seen == [2, 1]
print("PASS nested-handled-context")
