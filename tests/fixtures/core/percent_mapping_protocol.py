class Lookup:
    def __getitem__(self, key):
        return key.upper()


print("%(name)s:%(kind)s" % Lookup())


class DictLookup(dict):
    def __getitem__(self, key):
        return "value-for-" + key


print("%(column)s" % DictLookup())
