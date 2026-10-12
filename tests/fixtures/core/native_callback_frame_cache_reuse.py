import contextvars


class Box:
    def __init__(self, value):
        self._value = value
        self.value = value

    def get(self):
        return self._value


context = contextvars.Context()
box = Box(7)
OFFSET = 0


def read_value(item):
    return item.value + OFFSET


def call_method(item):
    return item.get() + OFFSET


original_read_code = read_value.__code__
original_get = Box.get

for _ in range(2):
    print(context.run(read_value, box))

OFFSET = 20
print(context.run(read_value, box))

Box.value = property(lambda item: item._value + 100)
print(context.run(read_value, box))
del Box.value
print(context.run(read_value, box))

for _ in range(2):
    print(context.run(call_method, box))

Box.get = lambda item: item._value + 200
print(context.run(call_method, box))
Box.get = original_get
print(context.run(call_method, box))


def changed_read_code(item):
    return item.value + 300


read_value.__code__ = changed_read_code.__code__
print(context.run(read_value, box))
read_value.__code__ = original_read_code
print(context.run(read_value, box))
