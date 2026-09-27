def rebuild(value):
    return value.__class__(value)


print(rebuild([1, 2]))
print(rebuild((3, 4)))
print(rebuild({'a': 5}))
print(rebuild('abc'))


def call_class(value, payload):
    return value.__class__(payload)


print(call_class([1], (6, 7)))
