from decimal import Decimal


values = (-1, 0, 1, (1 << 61) - 1, 1 << 61, -(1 << 61), 1.0, -1.0)
print([hash(value) for value in values])
print([hash(Decimal(value)) for value in (-1, 0, 1)])
print({1: "integer"}[Decimal(1)])
print({Decimal(-1): "decimal"}[-1])
