class Pair:
    def __init__(self, first, second):
        self.x = first
        self.x = second
        self.y = first

    def total(self):
        return self.x + self.y


pair = Pair(2, 3)
assert pair.total() == 5
assert vars(pair) == {"x": 3, "y": 2}


class Field:
    def __get__(self, instance, owner):
        if instance is None:
            return self
        return instance.saved

    def __set__(self, instance, value):
        instance.saved = value + 1


class DescriptorGuard:
    value = Field()

    def __init__(self, value):
        self.value = value


descriptor = DescriptorGuard(7)
assert descriptor.value == 8


setattr_calls = 0


class HookGuard:
    def __setattr__(self, name, value):
        global setattr_calls
        setattr_calls = setattr_calls + 1
        object.__setattr__(self, name, value)

    def __init__(self, value):
        self.value = value


hooked = HookGuard(9)
assert hooked.value == 9
assert setattr_calls == 1


try:
    Pair(1)
except TypeError:
    print("arity fallback")
else:
    raise AssertionError("constructor accepted missing argument")

print("attribute constructor guards ok")
