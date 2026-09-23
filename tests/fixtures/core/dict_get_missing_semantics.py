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
