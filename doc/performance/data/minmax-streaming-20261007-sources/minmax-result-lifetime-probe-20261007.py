import json

events = []


class Item:
    def __init__(self, value):
        self.value = value

    def __del__(self):
        events.append(self.value)


def plain_local():
    obj = Item('plain_local')
    value = obj.value
    del obj
    return list(events)


def winner_local():
    obj = max([Item('winner_local')], key=lambda value: 1)
    value = obj.value
    del obj
    return list(events)


observations = {}
observations['plain_local'] = plain_local()
observations['winner_local'] = winner_local()
obj = Item('plain_global')
value = obj.value
del obj
observations['plain_global'] = list(events)
obj = max([Item('winner_global')], key=lambda value: 1)
value = obj.value
del obj
observations['winner_global'] = list(events)
print(json.dumps(observations))
