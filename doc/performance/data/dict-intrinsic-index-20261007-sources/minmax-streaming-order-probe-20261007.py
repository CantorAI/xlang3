"""Observe iteration/key/comparison order and early failures against CPython."""
import json


def observe(which, failure=None):
    events = []

    class Rank:
        def __init__(self, value):
            self.value = value

        def __gt__(self, other):
            events.append(('compare', self.value, other.value))
            if failure == 'comparison':
                raise ValueError('comparison failed')
            return self.value > other.value

        def __lt__(self, other):
            events.append(('compare', self.value, other.value))
            if failure == 'comparison':
                raise ValueError('comparison failed')
            return self.value < other.value

    def values():
        for value in (1, 2, 3):
            events.append(('yield', value))
            yield value

    def key(value):
        events.append(('key', value))
        if failure == 'key' and value == 2:
            raise ValueError('key failed')
        return Rank(value)

    try:
        result = which(values(), key=key)
    except ValueError as error:
        result = str(error)
    return {'events': events, 'result': result}


observations = {}
for label, which in (('max', max), ('min', min)):
    for failure in (None, 'comparison', 'key'):
        observations[label + '_' + str(failure)] = observe(which, failure)
items = [1, 2]


def append_during_key(value):
    if value == 1:
        items.append(3)
    return value


observations['list_growth'] = max(items, key=append_during_key)
observations['empty_default_invalid_key'] = max([], default='fallback', key=42)
first, second = object(), object()
observations['ties_keep_first'] = max((first, second), key=lambda value: 1) is first
print(json.dumps(observations, sort_keys=True))
