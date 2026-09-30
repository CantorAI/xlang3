from functools import wraps


def replace_init_subclass(cls):
    original = cls.__init_subclass__.__func__

    @wraps(original)
    def wrapper(subclass, **kwargs):
        print('wrapper', subclass.__name__)
        return original(subclass, **kwargs)

    cls.__init_subclass__ = classmethod(wrapper)
    return cls


@replace_init_subclass
class Base:
    def __init_subclass__(cls, **kwargs):
        print('original', cls.__name__)
        return super().__init_subclass__(**kwargs)


class Child(Base):
    pass


print('created', Child.__name__)
