class Identity:
    pass


objects = [Identity() for _ in range(1000)]
mapping = dict.fromkeys(objects)
print("identity", len(mapping), sum(key in mapping for key in objects))
mapping["name"] = 1
print("mixed", objects[500] in mapping, mapping["name"])


class Collision:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 7

    def __eq__(self, other):
        return isinstance(other, Collision) and self.value == other.value


first = Collision("a")
equal = Collision("a")
other = Collision("b")
collisions = {first: 1, other: 2}
collisions[equal] = 3
print("collision", len(collisions), collisions[first], collisions[equal], collisions[other])
print("original", next(iter(collisions)) is first)
del collisions[equal]
print("deleted", len(collisions), first in collisions, other in collisions)
collisions.popitem()
collisions[Collision("c")] = 4
print("pop-reinsert", len(collisions), Collision("c") in collisions)
collisions.clear()
collisions[Collision("d")] = 5
print("clear-reinsert", len(collisions), collisions[Collision("d")])


class Broken:
    def __hash__(self):
        raise RuntimeError("bad hash")


try:
    collisions[Broken()] = 1
except Exception as error:
    print("hash-error", type(error).__name__, str(error))
