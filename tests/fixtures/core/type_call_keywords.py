class Meta(type):
    def __call__(cls, *args, **kwargs):
        return ("custom", args, kwargs)

    def construct(cls, *args, **kwargs):
        return super().__call__(*args, **kwargs)


class Example(metaclass=Meta):
    def __init__(self, value, *, label="default"):
        self.value = value
        self.label = label


print(Example(1, label="through-meta"))
created = Example.construct(2, label="through-type")
print(type(created).__name__, created.value, created.label)
created_positional = Example.construct(3)
print(type(created_positional).__name__, created_positional.value, created_positional.label)


class VarargsConstructor:
    def __init__(self, first, *rest, flag=False):
        self.first = first
        self.rest = rest
        self.flag = flag


varargs = VarargsConstructor(1, 2, 3)
print(varargs.first, varargs.rest, varargs.flag)
