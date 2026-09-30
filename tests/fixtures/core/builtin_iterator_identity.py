mapping = {"first": 1, "second": 2}
keys = iter(mapping.keys())
print(iter(keys) is keys, list(keys))

values = {1, 2}
items = iter(values)
print(iter(items) is items, sorted(items))


class KeySource:
    def __iter__(self):
        return iter(mapping.keys())


print(list(KeySource()))
