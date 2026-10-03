from decimal import Context, Decimal, getcontext


print([str(Decimal(value)) for value in (
    "0.00", "-0.00", "1234567.89", "1E+6", "0E+3", "1E-7", "-1.234E+20",
    "Infinity", "-Infinity", "NaN42", "-sNaN7",
)])

context = getcontext()
old_capitals = context.capitals
context.capitals = 0
print(str(Decimal("1E+6")))
context.capitals = old_capitals

print(Decimal("1234E+5").to_eng_string())
print(Decimal("1E+6").to_eng_string(context=Context(capitals=0)))


class DecimalSubclass(Decimal):
    pass


print(str(DecimalSubclass("1E+6")))


class DecimalSubclassWithString(Decimal):
    def __str__(self):
        return "subclass-decimal-string"


print(str(DecimalSubclassWithString("1E+6")))
