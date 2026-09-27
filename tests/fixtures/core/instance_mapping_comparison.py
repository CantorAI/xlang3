class Box:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return NotImplemented


class DictSubclass(dict):
    pass


box = Box(3)
box.__dict__
print(box == {'value': 3}, box != {'value': 3})
print(box == Box(3), box != Box(3))
print(DictSubclass({'value': 3}) == {'value': 3})
