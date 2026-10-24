from decimal import Decimal


values = (
    "0", "-0", "123.4500", "0.000001", "0.0000001", "1000000",
    "1E+2", "-NaN42", "sNaN3", "Infinity",
)
print([str(Decimal(value)) for value in values])


class DecimalSubclass(Decimal):
    def __str__(self):
        return "subclass-str"


print(str(DecimalSubclass("1.25")))
