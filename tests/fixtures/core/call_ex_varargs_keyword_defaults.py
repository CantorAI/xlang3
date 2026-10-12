def capture(*args, mode="initial"):
    return args, mode


print(capture(*[1, 2]))
print(capture(*(3, 4)))

empty = ()
print(capture(*empty)[0] is empty)

capture.__kwdefaults__ = {"mode": "updated"}
print(capture(*[5]))


class ListSubclass(list):
    def __iter__(self):
        print("subclass-iter")
        return super().__iter__()


print(capture(*ListSubclass([6])))
