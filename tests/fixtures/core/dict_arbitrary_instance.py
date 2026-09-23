class PlainObject:
    def __init__(self):
        self.value = 42


try:
    dict(PlainObject())
except TypeError as exc:
    print(type(exc).__name__)


class DictSubclass(dict):
    pass


print(dict(DictSubclass(answer=42)))
