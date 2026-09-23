class ParentMeta(type):
    pass


class Meta(ParentMeta):
    def __new__(mcls, name, bases, namespace, *, marker=None, **kwargs):
        created = super().__new__(mcls, name, bases, namespace, **kwargs)
        created.marker = marker
        return created


class Base(metaclass=Meta):
    pass


options = {"marker": "expanded"}
Dynamic = Meta("Dynamic", (Base,), {"value": 42}, **options)
print(Dynamic.__name__, Dynamic.marker, Dynamic.value)
print(type(Dynamic).__name__, issubclass(Dynamic, Base))
