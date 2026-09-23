class IntSubclass(int):
    pass


def passthrough(value):
    return value


integer = IntSubclass(7)
converted = float(integer)
print(converted, type(converted).__name__)

nan = float("nan")
returned = passthrough(nan)
print(returned is nan, returned == nan)
