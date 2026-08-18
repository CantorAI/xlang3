def get_value(self):
    return self._value + 1


def set_value(self, value):
    self._value = value * 2


class Box:
    value = property(get_value, set_value)

    def __init__(self):
        self._value = 3


class ReadOnly:
    @property
    def label(self):
        return "ready"


class Meter:
    def __init__(self):
        self._reading = 10

    @property
    def reading(self):
        return self._reading

    @reading.setter
    def reading(self, value):
        self._reading = value + 2

    @reading.deleter
    def reading(self):
        self._reading = 0


box = Box()
print(box.value)
box.value = 5
print(box.value)
print(isinstance(Box.value, property))

readonly = ReadOnly()
print(readonly.label)

meter = Meter()
print(meter.reading)
meter.reading = 20
print(meter.reading)
print(Meter.reading.fdel != None)
del meter.reading
print(meter.reading)
