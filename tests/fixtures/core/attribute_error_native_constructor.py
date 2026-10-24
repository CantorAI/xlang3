error = AttributeError("missing")
assert type(error) is AttributeError
assert error.args == ("missing",)
assert str(error) == "missing"
assert error.name is None
assert error.obj is None
print("PASS exact-attribute-error")

multiple = AttributeError(1, 2)
assert multiple.args == (1, 2)
assert str(multiple) == "(1, 2)"
print("PASS non-fast-argument-shape")


class CustomAttributeError(AttributeError):
    def __init__(self, value):
        self.args = ("custom", value)


custom = CustomAttributeError("value")
assert type(custom) is CustomAttributeError
assert custom.args == ("custom", "value")
print("PASS subclass-constructor-fallback")
