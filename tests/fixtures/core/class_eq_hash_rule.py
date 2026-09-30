class EqualOnly:
    def __eq__(self, other):
        return isinstance(other, EqualOnly)


class Child(EqualOnly):
    pass


class ExplicitHash(EqualOnly):
    def __hash__(self):
        return 17


class NotEqualOnly:
    def __ne__(self, other):
        return True


print(EqualOnly.__dict__['__hash__'] is None)
for cls in (EqualOnly, Child):
    try:
        hash(cls())
    except TypeError as error:
        print(str(error))
print(hash(ExplicitHash()), isinstance(hash(NotEqualOnly()), int))
