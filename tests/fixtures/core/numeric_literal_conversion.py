source = """
def marker(*args, **kwargs):
    def decorate(function):
        return function
    return decorate

@marker(
    [
        # Test cases for values with a fractional part.
        *[(round(i * 0.5, 1), None) for i in range(-2, 3)],
        (1_000_000.5, None),
    ],
    ids=repr,
)
def generated():
    return 1_000_000.49
"""
namespace = {}
exec(compile(source, "<numeric-literal-conversion>", "exec"), namespace)

print(namespace["generated"]())
print(1_000_000.5, 1_000_000.49, 1_0.5)

large = 2**63
converted = float(large)
print(converted, converted == large, large == converted)

class FloatSubclass(float):
    pass

print(int(FloatSubclass(1.0)))
print(float("  42.1  "), float("1_0.5"))
