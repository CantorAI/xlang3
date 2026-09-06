class Record:
    def __init__(self, values):
        self.values = values

    def __reduce_ex__(self, protocol):
        assert protocol == 4
        return Record, (self.values,), {"self": self}

    def __setstate__(self, state):
        assert state["self"] is self
        self.restored = True


class WithItems(list):
    def __reduce_ex__(self, protocol):
        return WithItems, (), {"tag": 42}, iter(self), None, apply_state

    def __setstate__(self, state):
        raise AssertionError("explicit state setter must take precedence")


def apply_state(obj, state):
    assert obj[0] is obj
    obj.tag = state["tag"]


class Named:
    def __reduce__(self):
        return "SINGLETON"


SINGLETON = Named()


class Registered:
    def __init__(self, value):
        self.value = value


def reduce_registered(obj):
    return Registered, (obj.value,)


class BadRecipe:
    def __init__(self, recipe):
        self.recipe = recipe

    def __reduce_ex__(self, protocol):
        return self.recipe


class ConstructorCycle:
    def __reduce_ex__(self, protocol):
        return ConstructorCycle, (self,)


class Constructed:
    def __init__(self):
        self.created = 42

    def __reduce__(self):
        return Constructed, (), {"restored": True}


class Slotted:
    __slots__ = ("value", "self")

    def __reduce__(self):
        return Slotted, (), (None, {"value": 42, "self": self})


class Holder:
    class Named:
        def __reduce__(self):
            return "Holder.SINGLETON"

    SINGLETON = Named()
