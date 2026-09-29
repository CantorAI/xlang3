class Base:
    def no_persistent_id(self, value):
        return None


class Child(Base):
    pass


item = Child()
print(item.no_persistent_id(object()))
print(item.no_persistent_id(value=object()))

try:
    item.no_persistent_id()
except TypeError as exc:
    print(type(exc).__name__)

try:
    item.no_persistent_id(1, 2)
except TypeError as exc:
    print(type(exc).__name__)


class Override(Base):
    def no_persistent_id(self, value):
        return value


print(Override().no_persistent_id(42))


def return_argument(self, value):
    return value


Child.no_persistent_id = return_argument
print(item.no_persistent_id("changed"))
