import math


class Indexed:
    def __index__(self):
        return 6


class Factor:
    def __init__(self, value):
        self.value = value

    def __mul__(self, other):
        return Factor(self.value * (other.value if isinstance(other, Factor) else other))

    def __rmul__(self, other):
        return Factor(other * self.value)


class Reflected:
    def __mul__(self, other):
        return NotImplemented

    def __rmul__(self, other):
        return 77


print(math.comb(10, 3), math.comb(5, 8), math.comb(Indexed(), 2))
print(math.comb(10**25, 2))
print(math.perm(8, 3), math.perm(8, 0), math.perm(5, 8))
print(math.factorial(20), math.perm(6))
print(math.log1p(1e-16), math.log1p(-0.5))
print(math.prod([2, 3, 5]), math.prod([], start=7), math.prod([2, 3], start=4))
print(math.prod([Factor(2), Factor(3)]).value,
      math.prod([Factor(2)], start=Factor(3)).value,
      math.prod([Reflected()]))
print(math.isqrt(0), math.isqrt(15), math.isqrt(10**50))
for call in (
    lambda: math.factorial(-1),
    lambda: math.comb(-1, 2),
    lambda: math.comb(3, -1),
    lambda: math.perm(3, -1),
    lambda: math.factorial(2.0),
    lambda: math.log1p(-1.0),
    lambda: math.isqrt(-1),
):
    try:
        call()
    except (TypeError, ValueError) as exc:
        print(type(exc).__name__)
