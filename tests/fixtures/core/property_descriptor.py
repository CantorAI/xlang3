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


class DescriptorInInit:
    def __init__(self, value):
        self.value = value

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        self._value = value * 3


class AssignedDescriptorInInit:
    def __init__(self, value):
        self.value = value

    value = property(get_value, set_value)


box = Box()
print(box.value)
box.value = 5
print(box.value)
print(isinstance(Box.value, property))

readonly = ReadOnly()
print(readonly.label)

meter = Meter()
print(meter.reading)

initialized = DescriptorInInit(7)
print(initialized.value, initialized.__dict__)
assigned_initialized = AssignedDescriptorInInit(4)
print(assigned_initialized.value, assigned_initialized.__dict__)
meter.reading = 20
print(meter.reading)
print(Meter.reading.fdel != None)
del meter.reading
print(meter.reading)


class SpoofedMissingDict:
    @property
    def __dict__(self):
        raise TypeError("vars() argument must have __dict__ attribute")


try:
    vars(SpoofedMissingDict())
except TypeError as error:
    print(type(error).__name__, str(error))
