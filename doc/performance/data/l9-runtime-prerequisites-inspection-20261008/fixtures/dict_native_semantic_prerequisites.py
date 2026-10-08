"""Strict semantic prerequisites for a held dictionary-index design.

CPython reference first; no timings, index allocation claims or implementation.
"""


kept = object()
mapping = {"kept": kept, "changed": 1}
assert dict.__init__(mapping, {"changed": 2, "added": 3}, keyword=4) is None
assert list(mapping) == ["kept", "changed", "added", "keyword"]
assert mapping["kept"] is kept
assert mapping == {"kept": kept, "changed": 2, "added": 3, "keyword": 4}
assert dict.__init__(mapping) is None
assert list(mapping) == ["kept", "changed", "added", "keyword"]


class DictChild(dict):
    def __setitem__(self, key, value):
        raise AssertionError("builtin dict initialization dispatched override")


child = DictChild({"kept": kept})
assert dict.__init__(child, {"added": 5}) is None
assert list(child) == ["kept", "added"] and child["kept"] is kept
print("PASS dict-init-merges-existing-storage")


true_first = True
float_first = float("1.0")
true_literal = {true_first: "first", 1: "last"}
float_literal = {float_first: "first", 1: "last"}
for result, first, key_type in (
    (true_literal, true_first, bool),
    (float_literal, float_first, float),
):
    assert len(result) == 1 and list(result.values()) == ["last"]
    actual = next(iter(result))
    assert actual is first and type(actual) is key_type
    assert result[True] == result[1] == result[1.0] == "last"
print("PASS equivalent-numeric-literal-first-key")


for first, key_type in ((true_first, bool), (float_first, float)):
    pairs = [(first, "first"), (1, "last")]
    result = {key: value for key, value in pairs}
    assert len(result) == 1 and list(result.values()) == ["last"]
    actual = next(iter(result))
    assert actual is first and type(actual) is key_type
    assert result[True] == result[1] == result[1.0] == "last"
print("PASS equivalent-numeric-comprehension-first-key")


class RefusesEqual(int):
    def __hash__(self):
        return 1

    def __eq__(self, other):
        self.comparisons.append(other)
        return False


unequal = RefusesEqual(1)
unequal.comparisons = []
result = {unequal: "subclass", 1: "integer"}
assert len(result) == 2
keys = list(result)
assert keys[0] is unequal and type(keys[1]) is int and keys[1] == 1
assert result[unequal] == "subclass" and result[1] == "integer"
assert unequal.comparisons and all(type(value) is int and value == 1 for value in unequal.comparisons)


class EqualError(LookupError):
    pass


failure = EqualError("numeric subclass equality marker")


class RaisesEqual(int):
    def __hash__(self):
        return 1

    def __eq__(self, other):
        self.comparisons += 1
        raise failure


raising = RaisesEqual(1)
raising.comparisons = 0
result = {raising: "subclass"}
try:
    result[1] = "integer"
except EqualError as caught:
    assert caught is failure
else:
    raise AssertionError("numeric subclass equality exception disappeared")
assert raising.comparisons == 1
assert len(result) == 1 and next(iter(result)) is raising
assert result[raising] == "subclass"
print("PASS numeric-subclass-equality-and-exception")
