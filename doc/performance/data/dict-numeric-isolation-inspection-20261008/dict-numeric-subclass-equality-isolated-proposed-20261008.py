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
