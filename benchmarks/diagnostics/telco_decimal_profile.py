"""Run the pyperformance telco workload once for XLang3 Decimal path profiling.

This preserves the official benchmark's arithmetic and record loop while
omitting pyperf's worker launcher, so XLANG3_DECIMAL_PROFILE output belongs to
the actual workload body rather than startup or benchmark calibration.
"""

from decimal import ROUND_DOWN, ROUND_HALF_EVEN, Context, Decimal, getcontext
import io
from struct import unpack
import sys
from time import perf_counter


def bench_telco(filename):
    getcontext().rounding = ROUND_DOWN
    rates = list(map(Decimal, ("0.0013", "0.00894")))
    twodig = Decimal("0.01")
    banker = Context(rounding=ROUND_HALF_EVEN)
    basictax = Decimal("0.0675")
    disttax = Decimal("0.0341")

    with open(filename, "rb") as infil:
        data = infil.read()

    infil = io.BytesIO(data)
    outfil = io.StringIO()
    start = perf_counter()
    for _ in range(1):
        infil.seek(0)
        sumT = Decimal("0")
        sumB = Decimal("0")
        sumD = Decimal("0")
        for _ in range(5000):
            datum = infil.read(8)
            if datum == "":
                break
            n, = unpack(">Q", datum)
            calltype = n & 1
            r = rates[calltype]
            p = banker.quantize(r * n, twodig)
            b = p * basictax
            b = b.quantize(twodig)
            sumB += b
            t = p + b
            if calltype:
                d = p * disttax
                d = d.quantize(twodig)
                sumD += d
                t += d
            sumT += t
            print(t, file=outfil)
        outfil.seek(0)
        outfil.truncate()
    return perf_counter() - start


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: telco_decimal_profile.py TELCO_DATA")
    print("one_telco_loop_seconds", bench_telco(sys.argv[1]))
