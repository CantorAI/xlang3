class ClassOnly:
    def __get__(self, instance, owner):
        if instance is None:
            return 'class value'
        raise AttributeError('class only')


class Base:
    pass


class Child(Base):
    pass


Child.signature = ClassOnly()
instance = Child()
print(Child.signature)
print(hasattr(instance, 'signature'))
for method in (object.__getattribute__, super(Child, instance).__getattribute__):
    try:
        method(instance, 'signature') if method is object.__getattribute__ else method('signature')
    except AttributeError as error:
        print(type(error).__name__, str(error))


class Data:
    def __get__(self, instance, owner):
        return 'descriptor'

    def __set__(self, instance, value):
        pass


Child.data = Data()
instance.__dict__['data'] = 'instance value'
print(object.__getattribute__(instance, 'data'))
