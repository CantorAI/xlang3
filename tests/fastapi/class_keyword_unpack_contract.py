events = []


class Meta(type):
    @classmethod
    def __prepare__(mcls, name, bases, **kwargs):
        events.append(("prepare", name, kwargs.get("flag")))
        return {}

    def __new__(mcls, name, bases, namespace, **kwargs):
        events.append(("new", name, kwargs.get("flag")))
        return super().__new__(mcls, name, bases, namespace)


class Base(metaclass=Meta):
    pass


class Derived(Base, **{"flag": 7}):
    answer = 42


print(Derived.answer, events[-2:])
