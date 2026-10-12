class SetterOnly:
    def __set__(self, instance, value):
        instance.__dict__["value"] = value
        instance.__dict__.pop("cached", None)


descriptor = SetterOnly()


class Owner:
    value = descriptor


class Child(Owner):
    pass


objects = [Owner(), Child()]
for instance in objects:
    assert instance.value is descriptor
    instance.cached = "stale"
    instance.value = {"node": 1}
    assert "cached" not in instance.__dict__


def getter(self, instance, owner):
    if instance is None:
        return self
    return "getter"


# Keep these read sites live across descriptor-type mutation. Its owner is
# unrelated to the descriptor's class, so owner-only version guards need cold
# invalidation when __get__/__set__/__delete__ changes on the descriptor type.
for iteration in range(6):
    if iteration == 2:
        SetterOnly.__get__ = getter
    if iteration == 4:
        del SetterOnly.__get__
    for instance in objects:
        expected = "getter" if 2 <= iteration < 4 else {"node": 1}
        assert instance.value == expected
        assert getattr(instance, "value") == expected


class Dormant:
    pass


empty_descriptor = Dormant()


class InitiallyPlain:
    value = empty_descriptor


instance = InitiallyPlain()
instance.__dict__["value"] = "dictionary"


def setter(self, instance, value):
    instance.__dict__["value"] = value


for iteration in range(6):
    if iteration == 2:
        Dormant.__get__ = getter
        Dormant.__set__ = setter
    if iteration == 4:
        del Dormant.__set__
    assert instance.value == ("getter" if 2 <= iteration < 4 else "dictionary")
    assert getattr(instance, "value") == ("getter" if 2 <= iteration < 4 else "dictionary")

print("setter-only descriptor reads and mutation ok")
