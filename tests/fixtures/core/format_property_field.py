class Item:
    def __init__(self, name):
        self._name = name

    @property
    def name(self):
        return self._name


item = Item("ready")
print("{0.name} {item.name}".format(item, item=item))
print("{0.name[0]}".format(item))
print("{0[1]} {1[0]}".format(["x", "y"], ("z",)))


class Lookup:
    def __getitem__(self, key):
        return key.upper()


print("{0[location]}".format(Lookup()))
