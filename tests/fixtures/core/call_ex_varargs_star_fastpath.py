def capture(*args):
    return args


items = (1, 2)
print(capture(*items), capture(*items) is items)

items = [3, 4]
print(capture(*items), capture(*items) is items)

empty = ()
print(capture(*empty), capture(*empty) is empty)


class ListSubclass(list):
    def __iter__(self):
        print("subclass-iter")
        return super().__iter__()


print(capture(*ListSubclass([5, 6])))
print(capture(*(7, 8), *[9, 10]))


def capture_keywords(*args, **kwargs):
    return args, kwargs


print(capture_keywords(*[11, 12], label="kept"))


def capture_with_default(*args, mode="default"):
    return args, mode


print(capture_with_default(*(item for item in [13, 14])))
print(capture_with_default(*[15, 16]))
print(capture_with_default(*(item for item in [])))
