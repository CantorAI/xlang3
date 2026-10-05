from decimal import Context, Decimal, Inexact, ROUND_DOWN, ROUND_FLOOR, Rounded, getcontext, setcontext
from contextvars import Context as ExecutionContext


exact = (
    Decimal("1.25") * Decimal("2.4"),
    Decimal("123.45") + Decimal("0.006"),
    Decimal("-8.5") * 12,
    3 * Decimal("1.25"),
    Decimal("1.2") + 3,
    Decimal("12345678901234567890") * Decimal("1234567890"),
    Decimal("12345678901234567890") * Decimal("0.00894"),
    Decimal("-12345678901234567890") * Decimal("0.00894"),
    Decimal("12345") * Decimal("0.00000"),
)
print([str(value) for value in exact])


context = getcontext()
context.prec = 3
context.rounding = ROUND_DOWN
rounded = (
    Decimal("1.239") * Decimal("1.2"),
    Decimal("9.876") + Decimal("0.1"),
)
print([str(value) for value in rounded], bool(context.flags[Inexact]), bool(context.flags[Rounded]))

# The XLang3 _decimal shim currently obtains Context from _pydecimal, so this
# pure-Python fallback exposes __dict__ even though CPython's native Context
# type does not. The native arithmetic path must still honor that dictionary.
context = Context(prec=3, traps=[])
context.__dict__["prec"] = 2
setcontext(context)
print(str(Decimal("1.239") + Decimal("0")))
context.__dict__["prec"] = 3


class DecimalSubclass(Decimal):
    pass


print(str(DecimalSubclass("1.25") * Decimal("2.4")))
print(Decimal("NaN") + Decimal("2"))

# A VM-level native Decimal opcode shortcut must respect class mutation and
# resolve the replacement method through ordinary Python special dispatch.
original_add = Decimal.__add__
def patched_add(self, other):
    return "patched-add"
Decimal.__add__ = patched_add
print(Decimal("1") + Decimal("2"))
Decimal.__add__ = original_add

# Context.quantize receives the same descriptor and instance-shadow checks as
# the direct VM native-call path.
original_context_quantize = Context.quantize
def patched_context_quantize(self, value, exponent):
    return Decimal("42")
Context.quantize = patched_context_quantize
print(Context(traps=[]).quantize(Decimal("1"), Decimal("1")))
Context.quantize = original_context_quantize

# Finite zero operands are common in telco's running totals. The native path
# must keep Decimal's signed-zero rules while avoiding a Python fallback.
print([
    str(Decimal("-0") + Decimal("-0")),
    str(Decimal("-0") * Decimal("2")),
    str(Decimal("-0") * Decimal("-2")),
])

context = Context(rounding=ROUND_FLOOR, traps=[])
setcontext(context)
print(str(Decimal("-1") + Decimal("1")))

context = Context(prec=2, traps=[])
setcontext(context)
rounded = Decimal("9.99") + Decimal("0")
print(str(rounded), bool(context.flags[Rounded]), bool(context.flags[Inexact]))

context = Context(prec=2, traps=[Inexact])
setcontext(context)
try:
    Decimal("1.23") + Decimal("0")
except Inexact:
    print("inexact-trapped")

# The native fast path must read the active ContextVar binding at each call;
# Context.run swaps that binding without replacing Decimal's cached accessor.
execution_context = ExecutionContext()
execution_context.run(setcontext, Context(prec=3, rounding=ROUND_DOWN, traps=[]))
context_result = execution_context.run(lambda: Decimal("1.239") + Decimal("0"))
print(str(context_result), getcontext().prec)
