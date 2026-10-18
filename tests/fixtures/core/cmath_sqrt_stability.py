import cmath
import math
import sys


def close_component(actual, expected):
    if expected == 0.0:
        return actual == 0.0 and math.copysign(1.0, actual) == math.copysign(1.0, expected)
    return math.isclose(actual, expected, rel_tol=8e-15, abs_tol=0.0)


def close_root(value, expected):
    result = cmath.sqrt(value)
    return type(result) is complex and close_component(result.real, expected.real) and close_component(result.imag, expected.imag)


print("negative-axis", cmath.sqrt(-4) == 2j, close_root(complex(-4, 0.0), complex(0.0, 2.0)), close_root(complex(-4, -0.0), complex(0.0, -2.0)))
print("signed-zero", all(close_root(complex(real, imag), complex(0.0, imag)) for real in [-0.0, 0.0] for imag in [-0.0, 0.0]))

squares = [(complex(3, 4), complex(2, 1)), (complex(-3, 4), complex(1, 2)), (complex(5, 12), complex(3, 2))]
print("known-squares", all(close_root(value, expected) and close_root(value.conjugate(), expected.conjugate()) for value, expected in squares))

minimum = float.fromhex("0x0.0000000000001p-1022")
near_cut = [(complex(-4, imag), complex(abs(imag) / 4.0, math.copysign(2.0, imag))) for imag in [1e-300, -1e-300, 4 * minimum, -4 * minimum, minimum, -minimum]]
large = sys.float_info.max
large_root = math.sqrt(large)
near_cut += [(complex(-large, imag), complex(abs(imag) / (2.0 * large_root), math.copysign(large_root, imag))) for imag in [1e-100, -1e-100]]
print("near-cut", all(close_root(value, expected) for value, expected in near_cut))

scaled = []
for exponent in [-1074, -1000, 1000]:
    scale = math.ldexp(1.0, exponent)
    root_scale = math.ldexp(1.0, exponent // 2)
    scaled.extend([(complex(3 * scale, 4 * scale), complex(2 * root_scale, root_scale)), (complex(-3 * scale, -4 * scale), complex(root_scale, -2 * root_scale))])
scaled += [(complex(large, 0.0), complex(large_root, 0.0)), (complex(-large, -0.0), complex(0.0, -large_root))]
# Reference components of sqrt(DBL_MAX + i*DBL_MAX), with relative tolerance
# per component; neither input magnitude nor x + hypot(x,y) fits a double.
scaled.append((complex(large, large), complex(float.fromhex("0x1.19435caffa9f9p+512"), float.fromhex("0x1.d203138f6c827p+510"))))
print("scaled-finite", all(close_root(value, expected) for value, expected in scaled))


def category(value):
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "-inf" if value < 0 else "+inf"
    if value == 0:
        return "-0" if math.copysign(1.0, value) < 0 else "+0"
    return "finite"


values = [-float("inf"), -4.0, -0.0, 0.0, 4.0, float("inf"), float("nan")]
special = [
    [("+inf", "-inf"), ("+0", "-inf"), ("+0", "-inf"), ("+0", "+inf"), ("+0", "+inf"), ("+inf", "+inf"), ("nan", "+inf")],
    [("+inf", "-inf"), None, None, None, None, ("+inf", "+inf"), ("nan", "nan")],
    [("+inf", "-inf"), None, None, None, None, ("+inf", "+inf"), ("nan", "nan")],
    [("+inf", "-inf"), None, None, None, None, ("+inf", "+inf"), ("nan", "nan")],
    [("+inf", "-inf"), None, None, None, None, ("+inf", "+inf"), ("nan", "nan")],
    [("+inf", "-inf"), ("+inf", "-0"), ("+inf", "-0"), ("+inf", "+0"), ("+inf", "+0"), ("+inf", "+inf"), ("+inf", "nan")],
    [("+inf", "-inf"), ("nan", "nan"), ("nan", "nan"), ("nan", "nan"), ("nan", "nan"), ("+inf", "+inf"), ("nan", "nan")],
]
checks = []
for i, real in enumerate(values):
    for j, imag in enumerate(values):
        if special[i][j] is not None:
            result = cmath.sqrt(complex(real, imag))
            checks.append((category(result.real), category(result.imag)) == special[i][j])
print("special-values", len(checks) == 33, all(checks))
