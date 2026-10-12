def combine(first, second=2, third=3):
    return first, second, third


print(combine(1), combine(1, 4), combine(1, 4, 5))
combine.__defaults__ = (8, 9)
print(combine(1))


class Receiver:
    def value(self, first, second=6):
        return first + second


print(Receiver().value(4))
