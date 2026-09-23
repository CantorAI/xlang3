import functools


class Base:
    def __init__(self, x, y=None, *, label=None):
        self.x = x
        self.y = y
        self.label = label


class Derived(Base):
    __init__ = functools.partialmethod(Base.__init__, label="bound")


value = Derived(1, y=2)
print(value.x, value.y, value.label)
