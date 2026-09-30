class PairIterable:
    def __init__(self):
        self.field = 9

    def __iter__(self):
        yield ('a', 1)
        yield ('b', 2)


instance = PairIterable()
instance.__dict__
print(hasattr(instance, 'keys'), dict(instance))


class DictSubclass(dict):
    pass


print(DictSubclass({'a': 1}).keys() == {'a': 1}.keys())
