# Negative immediate-value checks may skip instance-attribute lookup only
# after class-info validation and dynamic metaclass hooks have run.
for value in (None, False, 8, 1.5):
    assert not isinstance(value, tuple)
    assert isinstance(value, (tuple, object))
    assert isinstance(value, tuple | object)

calls = []


class CheckMeta(type):
    def __instancecheck__(cls, value):
        calls.append(value)
        if value == -1:
            raise LookupError("hook error")
        return value is None or value == 7


class Checked(metaclass=CheckMeta):
    pass


for value, expected in ((None, True), (False, False), (8, False), (1.5, False), (7, True)):
    assert isinstance(value, Checked) is expected
assert calls == [None, False, 8, 1.5, 7]
assert isinstance(7, (str, Checked))
assert isinstance(7, str | Checked)
assert not isinstance(8, str | Checked)
assert isinstance(7, (int, 999))  # Matching earlier tuple item short-circuits.

try:
    isinstance(-1, Checked)
except LookupError as error:
    assert str(error) == "hook error"
else:
    raise AssertionError("custom hook exception was lost")

for classes in (999, (str, 999)):
    try:
        isinstance(8, classes)
    except TypeError:
        pass
    else:
        raise AssertionError("invalid class information was ignored")


class IntSubclass(int):
    pass


assert isinstance(IntSubclass(8), int)
assert not isinstance(IntSubclass(8), tuple)
print("primitive typecheck dispatch", True)
