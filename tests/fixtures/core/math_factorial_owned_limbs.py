import math
import sys


class Indexed:
    def __init__(self, value):
        self.value = value
        self.calls = 0

    def __index__(self):
        self.calls += 1
        return self.value


class IntegerSubclass(int):
    def __mul__(self, other):
        raise AssertionError('factorial must ignore arithmetic overrides')


def raises(argument, kind):
    try:
        math.factorial(argument)
    except kind:
        return
    raise AssertionError('factorial accepted invalid input')


selected = (0, 1, 2, 12, 13, 20, 21, 31, 32, 63, 64, 65, 100,
            127, 128, 129, 255, 256, 257, 500, 1000, 5000)
expected = 1
retained = math.factorial(500)
for number in range(5001):
    if number:
        expected *= number
    if number <= 100 or number in selected:
        actual = math.factorial(number)
        assert actual == expected, number
        if number in selected:
            assert math.perm(number) == expected, number
            assert math.perm(number, None) == expected, number
            print(number, actual.bit_length(), actual % 97)
assert retained == math.factorial(500)
indexed = Indexed(500)
assert math.factorial(indexed) == retained
assert indexed.calls == 1
assert math.factorial(IntegerSubclass(21)) == math.factorial(21)
assert math.factorial(False) == math.factorial(True) == 1
for argument in (-1, -(1 << 100)):
    raises(argument, ValueError)
for argument in (2.0, '5', None):
    raises(argument, TypeError)
raises(1 << (31 if sys.platform == 'win32' else 63), OverflowError)
raises(1 << 100, OverflowError)
for function in (lambda: math.perm(1 << (31 if sys.platform == 'win32' else 63)),
                 lambda: math.perm(1 << 100, None)):
    try:
        function()
    except OverflowError:
        pass
    else:
        raise AssertionError('perm without k must use factorial range checks')
print('index, subclass, retained values, and platform range: OK')
