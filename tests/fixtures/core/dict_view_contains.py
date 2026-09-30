mapping = {'a': 1}
keys = mapping.keys()
items = mapping.items()
values = mapping.values()
print('methods', hasattr(keys, '__contains__'),
      hasattr(items, '__contains__'), hasattr(values, '__contains__'))
print('keys', keys.__contains__('a'), keys.__contains__('b'))
print('items', items.__contains__(('a', 1)), items.__contains__(('a', 2)))
mapping['b'] = 2
print('live', keys.__contains__('b'), items.__contains__(('b', 2)))
