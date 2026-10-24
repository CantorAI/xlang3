class DefaultHash:
    pass


value = DefaultHash()
print(hash(value) == object.__hash__(value))
value.__hash__ = lambda: 7
print(hash(value) == object.__hash__(value))


class CustomHash:
    def __hash__(self):
        return 37


print(hash(CustomHash()))


class Unhashable:
    def __eq__(self, other):
        return isinstance(other, Unhashable)


try:
    hash(Unhashable())
except TypeError:
    print("unhashable")
