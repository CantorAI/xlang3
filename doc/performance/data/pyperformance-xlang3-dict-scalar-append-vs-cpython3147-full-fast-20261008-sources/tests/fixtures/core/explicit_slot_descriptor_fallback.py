"""CPython-first correctness proposal; no benchmark or weakened expectations."""


def missing(operation):
    try:
        operation()
    except AttributeError:
        return
    raise AssertionError("missing explicit slot unexpectedly remained accessible")


class Closed:
    __slots__ = ("obj",)

    def __init__(self, value):
        self.obj = value


closed = Closed(11)
closed_slot = Closed.obj
del Closed.obj
missing(lambda: closed.obj)
missing(lambda: setattr(closed, "obj", 12))
missing(lambda: delattr(closed, "obj"))
missing(lambda: Closed(13))
assert closed_slot.__get__(closed, Closed) == 11
closed_slot.__set__(closed, 14)
assert closed_slot.__get__(closed, Closed) == 14
closed_slot.__delete__(closed)
missing(lambda: closed_slot.__get__(closed, Closed))
print("PASS explicit closed deletion")


class Mixed:
    __slots__ = ("obj", "__dict__")


mixed = Mixed()
mixed.obj = 21
mixed_slot = Mixed.obj
del Mixed.obj
missing(lambda: mixed.obj)
mixed.obj = 22
assert mixed.obj == mixed.__dict__["obj"] == 22
assert mixed_slot.__get__(mixed, Mixed) == 21
del mixed.obj
assert "obj" not in mixed.__dict__
missing(lambda: mixed.obj)
missing(lambda: delattr(mixed, "obj"))
mixed_slot.__set__(mixed, 23)
assert mixed_slot.__get__(mixed, Mixed) == 23
missing(lambda: mixed.obj)
Mixed.obj = mixed_slot
assert mixed.obj == 23
del mixed.obj
missing(lambda: mixed.obj)
print("PASS explicit mixed dynamic storage")


class Base:
    __slots__ = ("obj",)


class Child(Base):
    __slots__ = ()


inherited = Child()
inherited.obj = 31
inherited_slot = Base.obj
del Base.__slots__
del Base.obj
missing(lambda: inherited.obj)
missing(lambda: setattr(inherited, "obj", 32))
missing(lambda: delattr(inherited, "obj"))
assert inherited_slot.__get__(inherited, Child) == 31
inherited_slot.__set__(inherited, 33)
assert inherited_slot.__get__(inherited, Child) == 33
Base.obj = inherited_slot
assert inherited.obj == 33
print("PASS explicit inherited immutable history")
