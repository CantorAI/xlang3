"""CPython-first owner boundary proposal; no optimizer-specific allowances."""


def wrong_owner(operation):
    try:
        operation()
    except TypeError:
        return
    raise AssertionError("member descriptor accepted an unrelated instance")


class Owner:
    __slots__ = ("obj",)


class Derived(Owner):
    __slots__ = ()


Derived.alias = Owner.obj
member = Owner.obj
derived = Derived()
for value in range(4):
    derived.obj = value
    assert derived.alias == member.__get__(derived, Derived) == value
    derived.alias = value + 10
    assert derived.obj == value + 10
    member.__set__(derived, value + 20)
    assert derived.alias == value + 20
    del derived.alias
    try:
        derived.obj
    except AttributeError:
        pass
    else:
        raise AssertionError("valid alias failed to delete its owner's slot")
member.__set__(derived, 31)
member.__delete__(derived)
print("PASS slot owner derived aliases")


class Foreign:
    __slots__ = ("obj", "__dict__")


foreign = Foreign()
foreign_slot = Foreign.obj
foreign.obj = 41
Foreign.obj = member
Foreign.alias = member
foreign.__dict__["obj"] = 99
foreign.__dict__["alias"] = 98
for unused in range(4):
    for operation in (
        lambda: foreign.obj,
        lambda: setattr(foreign, "obj", 42),
        lambda: delattr(foreign, "obj"),
        lambda: foreign.alias,
        lambda: setattr(foreign, "alias", 43),
        lambda: delattr(foreign, "alias"),
        lambda: object.__getattribute__(foreign, "obj"),
        lambda: object.__setattr__(foreign, "obj", 44),
        lambda: object.__delattr__(foreign, "obj"),
        lambda: member.__get__(foreign, Foreign),
        lambda: member.__set__(foreign, 45),
        lambda: member.__delete__(foreign),
    ):
        wrong_owner(operation)
assert foreign_slot.__get__(foreign, Foreign) == 41
assert foreign.__dict__ == {"obj": 99, "alias": 98}
assert member.__get__(None, Foreign) is member
print("PASS slot foreign owner cold warm direct")
