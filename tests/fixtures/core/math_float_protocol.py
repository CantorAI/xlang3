import math
from decimal import Decimal


class FloatLike:
    def __init__(self, value):
        self.value = value

    def __float__(self):
        return self.value


print(math.isnan(Decimal("NaN")))
print(math.isinf(Decimal("Infinity")))
print(math.isfinite(Decimal("1.25")))
print(math.isnan(FloatLike(float("nan"))))
print(math.isinf(FloatLike(float("inf"))))
print(math.isfinite(FloatLike(3.5)))
