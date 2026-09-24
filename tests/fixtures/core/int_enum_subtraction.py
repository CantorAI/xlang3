from enum import IntEnum


class TrailType(IntEnum):
    STOP = 1
    CHOICE = 3


class Number(int):
    pass


class DecimalNumber(float):
    pass


class Override(int):
    def __sub__(self, other):
        return f"override-{other}"


print(7 - TrailType.CHOICE)
print(TrailType.CHOICE - 1)
print(TrailType.CHOICE - TrailType.STOP)
print(9 - Number(4), Number(9) - 4)
print(5.5 - DecimalNumber(1.5), DecimalNumber(5.5) - 1.5)
print(Override(5) - 2)
