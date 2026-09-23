class PrivateSlots:
    __slots__ = ("__value",)

    def __init__(self, value):
        object.__setattr__(self, "_PrivateSlots__value", value)

    def read(self):
        return self.__value


item = PrivateSlots(42)
print(item.read(), getattr(item, "_PrivateSlots__value"))
print(PrivateSlots.__slots__)
