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


class StringEqualKey:
    def __hash__(self):
        return hash("value")

    def __eq__(self, other):
        return other == "value"


# A string lookup must still compare against non-string stored keys with the
# same hash, so a type-specific index cannot skip mixed-key equality.
mixed = {StringEqualKey(): "custom-key"}
print(mixed["value"])

# Repeated exact-string GetItem sites may cache a live dict entry index. Mutation
# and changing the key at one bytecode site must still observe current entries.
changing = {"a": 1, "b": 2}
for _ in range(8):
    assert changing["a"] == 1
changing["a"] = 9
assert changing["a"] == 9
del changing["a"]
changing["c"] = 3
changing["a"] = 7
keys = ["b", "c", "a"]
expected = [2, 3, 7]
for index in range(len(keys)):
    assert changing[keys[index]] == expected[index]

# A string lookup that matches a custom non-string key must stay on the generic
# equality path and must not be mistaken for a cached string-index hit.
mixed_after_cache = {StringEqualKey(): "custom-key-after-cache"}
assert mixed_after_cache["value"] == "custom-key-after-cache"
print("string getitem cache mutation and equality guards: OK")
