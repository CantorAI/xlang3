import asyncio


class Addable:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return Addable(self.value + other.value)


def add_sync(left, right):
    result = left + right
    return result.value


def add_chain(first, second, third):
    return first + second + third


async def add_async(left, right):
    result = left + right
    return result.value


print(add_sync(Addable(20), Addable(22)))
print(asyncio.run(add_async(Addable(19), Addable(23))))
print(add_chain(Addable(10), Addable(20), Addable(12)).value)


class ReflectedAddable:
    def __radd__(self, other):
        return Addable(other + 1)


def reflected_add_chain(first, middle, last):
    return first + middle + last


print(reflected_add_chain(1, ReflectedAddable(), Addable(40)).value)


def reflected_add_assignment(left, right):
    result = left + right
    return result.value


print(reflected_add_assignment(1, ReflectedAddable()))


events = []


class OrderedNumber:
    def __init__(self, value):
        self.value = value

    def __mul__(self, other):
        events.append("mul")
        return OrderedNumber(self.value * other.value)

    def __add__(self, other):
        events.append("add")
        return OrderedNumber(self.value + other.value)

    def __sub__(self, other):
        events.append("sub")
        return OrderedNumber(self.value - other.value)


def ordered_chain(a, b):
    result = a + b * OrderedNumber(2) - OrderedNumber(1)
    return result.value


print(ordered_chain(OrderedNumber(10), OrderedNumber(3)), ",".join(events))


def overflow_chain():
    value = 9223372036854775807
    value = value + 1 * 2 - 1
    return value


def bool_chain():
    value = True + 2 * 3 - 1
    return value


print(overflow_chain())
print(bool_chain())
