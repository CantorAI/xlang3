"""Telco-shaped Decimal arithmetic without its external input-data dependency."""
from decimal import Context, Decimal, ROUND_DOWN, ROUND_HALF_EVEN, getcontext
from io import StringIO


def main():
    getcontext().rounding = ROUND_DOWN
    rates = (Decimal("0.0013"), Decimal("0.00894"))
    twodig = Decimal("0.01")
    banker = Context(rounding=ROUND_HALF_EVEN)
    basictax = Decimal("0.0675")
    disttax = Decimal("0.0341")
    output = StringIO()
    sum_total = Decimal("0")
    sum_basic = Decimal("0")
    sum_district = Decimal("0")
    for i in range(5000):
        # Stable positive 32-bit amounts keep the arithmetic in telco's usual
        # finite, short-coefficient region while exercising both rate branches.
        amount = 10000000 + i * 7919 + (i % 17) * 101
        calltype = amount & 1
        rate = rates[calltype]
        price = banker.quantize(rate * amount, twodig)
        basic = price * basictax
        basic = basic.quantize(twodig)
        sum_basic += basic
        total = price + basic
        if calltype:
            district = price * disttax
            district = district.quantize(twodig)
            sum_district += district
            total += district
        sum_total += total
        print(total, file=output)
    print(len(output.getvalue()), str(sum_total), str(sum_basic), str(sum_district))


main()
