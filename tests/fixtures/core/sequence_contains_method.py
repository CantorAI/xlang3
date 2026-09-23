class EqualByValue:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return isinstance(other, EqualByValue) and self.value == other.value


needle = EqualByValue(3)
print([EqualByValue(3)].__contains__(needle))
print([EqualByValue(4)].__contains__(needle))
print((EqualByValue(3),).__contains__(needle))
print((EqualByValue(4),).__contains__(needle))
print(hasattr([], "__contains__"), hasattr((), "__contains__"))
