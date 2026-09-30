def delegated(items):
    yield from items or ()


def conditional(items):
    yield from items if items is not None else ()


print(list(delegated(None)))
print(list(delegated([1, 2])))
print(list(conditional(None)))
print(list(conditional((3, 4))))
