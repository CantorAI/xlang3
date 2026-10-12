class OverrideDict(dict):
    def keys(self):
        raise AssertionError("dict expansion must use the built-in storage")

    def __getitem__(self, key):
        raise AssertionError("dict expansion must use the built-in storage")


def capture(**kwargs):
    return kwargs


source = OverrideDict(value=42)
assert capture(**source) == {"value": 42}
assert dict(**source) == {"value": 42}


class Receiver:
    def __init__(self, **kwargs):
        self.kwargs = kwargs


assert Receiver(**source).kwargs == {"value": 42}

try:
    capture(value=1, **source)
except TypeError:
    pass
else:
    raise AssertionError("duplicate keyword was accepted")

try:
    capture(**OverrideDict({1: "not a keyword"}))
except TypeError:
    pass
else:
    raise AssertionError("non-string keyword was accepted")

print("dict-subclass ** expansion follows built-in storage: OK")
