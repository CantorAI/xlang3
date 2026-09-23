class Key:
    def __init__(self, value, hash_value=17):
        self.value = value
        self.hash_value = hash_value

    def __hash__(self):
        return self.hash_value

    def __eq__(self, other):
        return isinstance(other, Key) and self.value == other.value


first = Key("same")
equal = Key("same")
collision = Key("different")
different_hash = Key("same", 23)

mapping = {first: "initial"}
print(mapping[equal], equal in mapping, mapping.get(equal), len(mapping))
mapping[equal] = "replaced"
mapping[collision] = "collision"
mapping[different_hash] = "different-hash"
print(mapping[first], mapping[collision], mapping[different_hash], len(mapping))
print(mapping.pop(equal), len(mapping), first in mapping)


class Unhashable:
    __hash__ = None


try:
    {Unhashable(): 1}
except TypeError:
    print("unhashable")
