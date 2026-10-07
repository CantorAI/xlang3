import builtins


class Token(str):
    pass


class Render:
    def __repr__(self):
        return "render-é-😀"


class SlotLookup:
    def __getattribute__(self, name):
        if name == "__repr__":
            return lambda: "wrong-instance-hook"
        return object.__getattribute__(self, name)

    def __repr__(self):
        return "class-slot"


token = Token("python: options['table']")
values = [(token, 3, 20), [token, Render()], {token: Render()},
          {token}, frozenset([token]), Render()]
for value in values:
    assert "%r" % (value,) == repr(value)
    assert "%a" % (value,) == ascii(value)
    assert b"%r" % (value,) == ascii(value).encode("ascii")
    assert b"%a" % (value,) == ascii(value).encode("ascii")
    assert "{!r}".format(value) == repr(value)
    assert "{!a}".format(value) == ascii(value)
assert eval("%r" % ((token, 3, 20),))[0] == token
print("nested native and Python repr", True)

value = Render()
assert "%12.8a" % value == "    render-\\"
assert b"%12.8r" % value == b"    render-\\"
assert "{!a}".format(value) == "render-\\xe9-\\U0001f600"
print("ASCII conversion and precision", True)

slot = SlotLookup()
slot.__repr__ = lambda: "wrong-instance-attribute"
assert repr(slot) == "class-slot"
assert "%r" % slot == "class-slot"
assert b"%r" % slot == b"class-slot"
assert "{!r}".format(slot) == "class-slot"
original_repr, original_ascii = builtins.repr, builtins.ascii
try:
    builtins.repr = lambda value: "wrong-rebound-repr"
    builtins.ascii = lambda value: "wrong-rebound-ascii"
    assert "%r" % slot == "class-slot"
    assert "%a" % slot == "class-slot"
    assert b"%r" % slot == b"class-slot"
    assert "{!r}".format(slot) == "class-slot"
finally:
    builtins.repr, builtins.ascii = original_repr, original_ascii
print("type slots and intrinsic lookup", True)

cycle = [token]
cycle.append(cycle)
assert "%r" % cycle == repr(cycle)
assert b"%a" % cycle == ascii(cycle).encode("ascii")
print("recursive container formatting", True)


class Bad:
    def __repr__(self):
        return 7


class Fails:
    def __repr__(self):
        raise RuntimeError("repr-failed")


conversions = [lambda value: "%r" % value,
               lambda value: "%a" % value,
               lambda value: b"%r" % value,
               lambda value: b"%a" % value,
               lambda value: "{!r}".format(value),
               lambda value: "{!a}".format(value)]
for conversion in conversions:
    try:
        conversion(Bad())
    except TypeError:
        pass
    else:
        raise AssertionError("non-string repr accepted")
    try:
        conversion(Fails())
    except RuntimeError as error:
        assert str(error) == "repr-failed"
    else:
        raise AssertionError("repr exception swallowed")
print("repr errors preserved", True)
