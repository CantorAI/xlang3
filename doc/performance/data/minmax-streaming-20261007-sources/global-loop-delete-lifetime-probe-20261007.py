import json
events = []
snapshots = []


class Item:
    def __init__(self, value):
        self.value = value

    def __del__(self):
        events.append(self.value)


for number in (1, 2):
    obj = Item(number)
    value = obj.value
    del obj
    snapshots.append(list(events))
print(json.dumps(snapshots))
