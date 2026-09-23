class TransformingNamespace(dict):
    def __setitem__(self, key, value):
        if key == "first" or key == "second":
            value = value * 10
        super().__setitem__(key, value)


class TransformingMeta(type):
    @classmethod
    def __prepare__(metacls, name, bases):
        return TransformingNamespace()


class Prepared(metaclass=TransformingMeta):
    first = 2
    second = first + 1
    total = first + second


print(Prepared.first, Prepared.second, Prepared.total)
