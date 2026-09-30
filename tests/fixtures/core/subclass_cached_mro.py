class Left:
    pass


class Right:
    pass


class Derived(Left):
    pass


class Descendant(Derived):
    pass


def relationships():
    return (issubclass(Derived, Left), issubclass(Derived, Right),
            issubclass(Descendant, Left), issubclass(Descendant, Right))


for _ in range(30):
    assert relationships() == (True, False, True, False)
print("before", relationships())
Derived.__bases__ = (Right,)
print("after", relationships())
Derived.__bases__ = (Left,)
print("restored", relationships())


class Both(Left, Right):
    pass


print("diamond", issubclass(Both, Left), issubclass(Both, Right),
      issubclass(Both, Both), issubclass(Left, Both))


class Number(int):
    pass


class Real(float):
    pass


print("numeric", bool(Number()), bool(Number(7)), bool(Real()), bool(Real(0.5)))


class Length(Left):
    pass


item = Length()
for _ in range(30):
    assert bool(item)
Right.__len__ = lambda self: 0
Length.__bases__ = (Right,)
print("truth-base", bool(item))
Right.__len__ = lambda self: 2
print("truth-mutation", bool(item))
Length.__bases__ = (Left,)
print("truth-restored", bool(item))
