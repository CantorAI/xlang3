import weakref


def descend(depth):
    if depth:
        return descend(depth - 1)
    return weakref.WeakKeyDictionary()


value = descend(6)
print(type(value).__name__, len(value))
