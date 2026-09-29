class IterTarget:
    def __iter__(self):
        return iter((1, 2))


class HookedIterTarget:
    def __iter__(self):
        return iter((3,))

    def __getattribute__(self, name):
        if name == "__iter__":
            return lambda: iter((9,))
        return object.__getattribute__(self, name)


class StaticIterTarget:
    @staticmethod
    def __iter__():
        return iter((4,))


class InstanceOnlyIterTarget:
    pass


target = IterTarget()
target.__iter__ = lambda: iter((8,))
instance_only = InstanceOnlyIterTarget()
instance_only.__iter__ = lambda: iter((9,))
try:
    list(instance_only)
except TypeError:
    instance_only_result = "TypeError"
print(list(target), list(HookedIterTarget()), list(StaticIterTarget()), instance_only_result)
