from collections import OrderedDict


ordered = OrderedDict((key, key.upper()) for key in ("type", "event", "body"))
ordered["seq"] = 1
for key in ("seq", "type", "event", "body"):
    value = ordered[key]
    del ordered[key]
    ordered[key] = value
print(list(ordered.items()))


class TrackingDict(dict):
    def __init__(self):
        super().__init__(present=1)
        self.deleted = []

    def __delitem__(self, key):
        self.deleted.append(key)
        return super().__delitem__(key)


tracking = TrackingDict()
del tracking["present"]
print(len(tracking), tracking.deleted)
