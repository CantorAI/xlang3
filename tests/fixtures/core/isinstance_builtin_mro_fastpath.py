class Parent:
    pass


class Child(Parent):
    pass


class Other:
    pass


def index_is_tuple(value):
    return isinstance(value, tuple)


print(
    "default",
    isinstance(Child(), Parent),
    isinstance(Other(), Parent),
    issubclass(Child, Parent),
    issubclass(Parent, Child),
    isinstance(True, int),
    isinstance(1, tuple),
)
print("tuple-call-cache", index_is_tuple((1, 2)), index_is_tuple(1))

saved_isinstance = isinstance
try:
    isinstance = lambda value, klass: "rebound"
    print("rebound-tuple-call-cache", index_is_tuple(1))
finally:
    isinstance = saved_isinstance

saved_instancecheck = type.__dict__["__instancecheck__"]
saved_subclasscheck = type.__dict__["__subclasscheck__"]
try:
    type.__instancecheck__ = lambda cls, value: False
    type.__subclasscheck__ = lambda cls, value: False
    print(
        "patched",
        isinstance(Child(), Parent),
        issubclass(Child, Parent),
        isinstance(1, int),
    )
    print("patched-tuple-call-cache", index_is_tuple((1, 2)), index_is_tuple(1))
    type.__instancecheck__ = lambda cls, value: True
    print("patched-true-tuple-call-cache", index_is_tuple(1))
finally:
    type.__instancecheck__ = saved_instancecheck
    type.__subclasscheck__ = saved_subclasscheck

print(
    "restored",
    isinstance(Child(), Parent),
    issubclass(Child, Parent),
    isinstance(1, int),
)
print("restored-tuple-call-cache", index_is_tuple((1, 2)), index_is_tuple(1))
