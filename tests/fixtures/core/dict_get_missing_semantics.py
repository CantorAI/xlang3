class MissingDict(dict):
    def __init__(self):
        super().__init__()
        self.calls = []

    def __missing__(self, key):
        self.calls.append(key)
        return "missing:" + key


mapping = MissingDict()
print(mapping.get("key"), mapping.calls)
print(mapping.get("key", "default"), mapping.calls)
print(mapping["key"], mapping.calls)
print("%(other)s" % mapping, mapping.calls)

# Exact-type class keys can use the VM's guarded dict.get fast path.
print({int: "builtin"}.get(int))

# A custom metaclass may run Python code for hashing and equality; dict.get
# must continue through the normal method path for these class keys.
class KeyMeta(type):
    hashes = 0
    compares = 0

    def __hash__(cls):
        KeyMeta.hashes += 1
        return 17

    def __eq__(cls, other):
        KeyMeta.compares += 1
        return cls is other

class KeyA(metaclass=KeyMeta):
    pass

class KeyB(metaclass=KeyMeta):
    pass

custom_keys = {KeyA: "A"}
print(custom_keys.get(KeyA), custom_keys.get(KeyB, "missing"), KeyMeta.hashes, KeyMeta.compares)
