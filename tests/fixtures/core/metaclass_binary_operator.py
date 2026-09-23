class Meta(type):
    def __add__(cls, other):
        return (cls.__name__, other)


class Example(metaclass=Meta):
    def __add__(self, other):
        return "instance", other


print(Example + 7)
print(Example().__add__(8))
print(Example.__add__(Example(), 9))
