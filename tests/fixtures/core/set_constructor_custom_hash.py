class Key:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        return isinstance(other, Key) and self.name == other.name


values = (Key("a"), Key("a"), Key("b"), Key("b"))
print(len(set(values)), len(frozenset(values)))
print(Key("a") in set(values), Key("c") in set(values))
