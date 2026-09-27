class Descriptor:
    def __get__(self, instance, owner):
        value = instance.value

        def read():
            return value

        return read


class Box:
    read = Descriptor()

    def __init__(self, value):
        self.value = value


box = Box(42)
held = box.read
print("held", held())
print("temporary", box.read())
