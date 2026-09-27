from fractions import Fraction


for value in (False, True):
    print(
        type(value.numerator).__name__,
        value.numerator,
        value.denominator,
        value.real,
        value.imag,
        Fraction(value),
    )
