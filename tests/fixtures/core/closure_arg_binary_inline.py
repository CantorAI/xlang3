def make_offsetter(initial):
    offset = initial

    def add_offset(value):
        return value + offset

    def set_offset(value):
        nonlocal offset
        offset = value

    return add_offset, set_offset


add, set_offset = make_offsetter(5)
print("closure-binary", add(7))
set_offset(11)
print("closure-cell-live", add(7))
print("closure-bigint", add(2**63 - 1))


def make_subtractor(offset):
    def subtract(value):
        return offset - value

    return subtract


print("closure-free-lhs", make_subtractor(30)(8))


def make_mutable_add():
    offset = 3

    def add_value(value):
        return value + offset

    def set_offset(value):
        nonlocal offset
        offset = value

    return add_value, set_offset


mutating_add, set_offset = make_mutable_add()
print("closure-kind-before", mutating_add(4))
set_offset("x")
try:
    mutating_add(4)
except TypeError:
    print("closure-kind-fallback", "TypeError")


class RightAdd:
    def __radd__(self, value):
        return ("radd", value)


fallback = make_offsetter(RightAdd())[0]
print("closure-custom-add", fallback(9))


def make_empty_capture():
    value = 1

    def read(offset):
        return offset + value

    def clear():
        nonlocal value
        del value

    return read, clear


read, clear = make_empty_capture()
print("closure-empty-warm", read(2))
clear()
try:
    read(2)
except NameError:
    print("closure-empty-cell", "NameError")
else:
    print("closure-empty-cell", "unexpected success")


original_code = add.__code__


def make_multiplier(factor):
    def multiply(value):
        return value * factor

    return multiply


add.__code__ = make_multiplier(3).__code__
print("closure-code-replaced", add(7))
add.__code__ = original_code
print("closure-code-restored", add(7))
