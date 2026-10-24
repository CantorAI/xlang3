class Box:
    def __init__(self, value):
        self.value = "initial:" + str(value)


def construct(cls, args):
    return cls(*args)


print(construct(Box, (1,)).value)
print(construct(Box, (2,)).value)


def replacement_init(self, value):
    self.value = "patched:" + str(value)


Box.__init__ = replacement_init
print(construct(Box, (3,)).value)


class Base:
    def __init__(self, value):
        self.value = "base:" + str(value)


class Child(Base):
    pass


print(construct(Child, (4,)).value)


def replacement_base_init(self, value):
    self.value = "new-base:" + str(value)


Base.__init__ = replacement_base_init
print(construct(Child, (5,)).value)


descriptor_gets = 0


class InitDescriptor:
    def __get__(self, instance, owner):
        global descriptor_gets
        descriptor_gets += 1

        def initialize(value):
            instance.value = "descriptor:" + str(value)

        return initialize


class Described:
    __init__ = InitDescriptor()


print(construct(Described, (6,)).value)
print(construct(Described, (7,)).value)
print(descriptor_gets)
