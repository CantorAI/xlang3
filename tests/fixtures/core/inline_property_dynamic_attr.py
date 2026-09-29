class Counter:
    def __init__(self):
        self._value = 1

    @property
    def value(self):
        return self._value + 1

    @value.setter
    def value(self, item):
        self._value = item - 1


counter = Counter()
counter.value = 11
print(counter.value)
print(counter.value)
attributes = vars(counter)
attributes["_value"] = 40
print(counter.value)
counter.value = 51
print(attributes["_value"])
print(counter.value)


class AddOverride:
    def __add__(self, other):
        return 73


class Flexible:
    def __init__(self):
        self._value = AddOverride()

    @property
    def value(self):
        return self._value + 1


print(Flexible().value)


class Hooked:
    def __init__(self):
        self._value = 2

    @property
    def value(self):
        return self._value + 1

    def __getattribute__(self, name):
        if name == "_value":
            return 19
        return object.__getattribute__(self, name)


print(Hooked().value)
