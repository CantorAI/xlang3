from decimal import (
    Context,
    Inexact,
    ROUND_05UP,
    ROUND_CEILING,
    ROUND_DOWN,
    ROUND_FLOOR,
    ROUND_HALF_DOWN,
    ROUND_HALF_EVEN,
    ROUND_HALF_UP,
    ROUND_UP,
    Rounded,
    Decimal,
)


cases = (
    ("1.245", "0.01", ROUND_HALF_EVEN),
    ("1.255", "0.01", ROUND_HALF_EVEN),
    ("1.251", "0.01", ROUND_HALF_DOWN),
    ("-1.251", "0.01", ROUND_CEILING),
    ("-1.251", "0.01", ROUND_FLOOR),
    ("1.231", "0.01", ROUND_UP),
    ("1.231", "0.01", ROUND_DOWN),
    ("0.004", "0.1", ROUND_UP),
    ("0.045", "0.01", ROUND_05UP),
    ("0.055", "0.01", ROUND_05UP),
    ("1.2300", "0.01", ROUND_HALF_EVEN),
    ("1.23", "0.0001", ROUND_HALF_EVEN),
)
for value, quantum, rounding in cases:
    context = Context(prec=8, rounding=rounding, traps=[])
    result = context.quantize(Decimal(value), Decimal(quantum))
    print(str(result), bool(context.flags[Inexact]), bool(context.flags[Rounded]))

context = Context(prec=8, traps=[Inexact])
try:
    context.quantize(Decimal("1.239"), Decimal("0.01"))
except Inexact:
    print("inexact-trapped", bool(context.flags[Inexact]))
