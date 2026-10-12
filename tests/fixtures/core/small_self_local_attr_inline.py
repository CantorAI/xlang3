class SlotGetter:
    __slots__ = ("value", "__dict__")

    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


def invoke(obj):
    # Keep one warmed CallLocalMethod site while the slot value and method
    # shadow change underneath it.
    return obj.get()


slot_item = SlotGetter(7)
print(invoke(slot_item))
slot_item.value = 8
print(invoke(slot_item))
slot_item.get = lambda: 99
print(invoke(slot_item))
del slot_item.get
print(invoke(slot_item))


class PropertyGetter:
    def __init__(self, value):
        self._value = value

    @property
    def value(self):
        return self._value + 1

    def get(self):
        return self.value


print(invoke(PropertyGetter(10)))
