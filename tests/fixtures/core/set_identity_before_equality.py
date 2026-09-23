class BooleanTrap:
    def __bool__(self):
        raise AssertionError("equality result evaluated")


class Key:
    comparisons = 0

    def __init__(self, hash_value):
        self.hash_value = hash_value

    def __hash__(self):
        return self.hash_value

    def __eq__(self, other):
        Key.comparisons += 1
        return BooleanTrap()


key = Key(1)
values = {key}
print(key in values)
values.discard(key)
print(len(values), Key.comparisons)

stored = Key(1)
values = {stored}
print(Key(2) in values, Key.comparisons)

print(sorted({"left": 1} | {"right": 2}.keys()))
print(sorted({"left": 1}.keys() | {"right": 2}))


class IntChild(int):
    pass


class CustomHashInt(int):
    def __hash__(self):
        return 99


integer = IntChild(21)
print(hash(integer) == hash(21), 21 in {integer})
custom = CustomHashInt(1)
print(hash(custom), 1 in {custom})
