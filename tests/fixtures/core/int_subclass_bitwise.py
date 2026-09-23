from enum import IntEnum


class Number(int):
    pass


class Bits(IntEnum):
    ZERO = 0
    ONE = 1
    TWO = 2


for value in (Number(3) ^ 1, 1 ^ Number(3), Bits.ZERO ^ Bits.ONE, Bits.ONE ^ Bits.TWO):
    print(value, type(value).__name__)
