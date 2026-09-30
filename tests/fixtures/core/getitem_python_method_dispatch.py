def read(value, index):
    return value[index]


class Wrapped:
    def __init__(self, data):
        self.data = data

    def __getitem__(self, index):
        return self.data[index]


wrapped = Wrapped([10, 20, 30])
assert read(wrapped, 0) == 10
assert read(wrapped, -1) == 30

# The call cache follows class rebinding and still uses the new Python body.
original_getitem = Wrapped.__getitem__
Wrapped.__getitem__ = lambda self, index: ("patched", index)
assert read(wrapped, 1) == ("patched", 1)
Wrapped.__getitem__ = original_getitem
assert read(wrapped, 2) == 30

# Special methods are resolved through the class, not an instance attribute.
wrapped.__getitem__ = lambda index: "instance shadow must be ignored"
assert read(wrapped, 1) == 20


class Parent:
    def __getitem__(self, index):
        return ("parent", index)


class Inherited(Parent):
    pass


assert read(Inherited(), 4) == ("parent", 4)


class Descriptor:
    def __get__(self, instance, owner):
        return lambda index: ("descriptor", index)


class DescriptorItem:
    __getitem__ = Descriptor()


assert read(DescriptorItem(), 5) == ("descriptor", 5)


def make_closure_item(multiplier):
    class ClosureItem:
        def __getitem__(self, index):
            return index * multiplier

    return ClosureItem()


assert read(make_closure_item(7), 6) == 42

try:
    read(wrapped, 9)
except IndexError:
    pass
else:
    raise AssertionError("out-of-range indexing should preserve IndexError")

print("getitem-python-method-dispatch-ok")
