class Box:
    def __init__(self, value):
        self.value = value


def identity(value):
    return value


def same(left, right):
    return left is right


def produce(value):
    yield value


def needs_two(left, right):
    return left is right


def call_then_use(value):
    result = identity(value)
    return result is value, value.value


box = Box(17)
received = identity(box)
print(received is box, received.value, box.value)
print(same(box, box))
print(next(produce(box)) is box)
try:
    needs_two(box)
except TypeError:
    print("arity fallback", box.value)
print(call_then_use(box))
