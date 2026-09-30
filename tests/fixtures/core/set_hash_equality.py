class Key:
    def __init__(self, number):
        self.number = number

    def __hash__(self):
        return 1

    def __eq__(self, other):
        return isinstance(other, Key) and self.number == other.number


first = Key(7)
same = Key(7)
different = Key(8)
print(first == same, hash(first) == hash(same), len({first, same, different}))
