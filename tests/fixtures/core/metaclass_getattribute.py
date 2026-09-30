class Meta(type):
    def __getattribute__(cls, name):
        if name == 'item':
            print('hook', name)
        if name == 'missing':
            raise AttributeError(name)
        return super().__getattribute__(name)

    def __getattr__(cls, name):
        if name == 'missing':
            return 19
        raise AttributeError(name)


class Example(metaclass=Meta):
    item = 7

    def method(cls):
        return 11


print('direct', Example.item)
print('builtin', getattr(Example, 'item'))
print('method', Example.method(Example))
print('missing', Example.missing)
print('default', getattr(Example, 'other', 23))
