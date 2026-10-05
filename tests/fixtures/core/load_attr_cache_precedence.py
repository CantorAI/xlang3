class Box:
    pass


box = Box()
box.value = "instance"


def read_value(target):
    return target.value


print(read_value(box))
print(read_value(box))
print(read_value(box))
box.value = "updated instance"
print(read_value(box))

Box.value = property(lambda self: "descriptor")
print(read_value(box))


class Custom:
    def __getattribute__(self, name):
        if name == "value":
            return "custom"
        return object.__getattribute__(self, name)


custom = Custom()
custom.value = "instance"


def read_custom(target):
    return target.value


print(read_custom(custom))
print(read_custom(custom))
