import ast


def double(function):
    def wrapped(value):
        return function(value) * 2

    return wrapped


module = ast.parse("@double\ndef calculate(value):\n    return value + 1\n")
namespace = {"double": double}
exec(compile(module, "<decorated ast>", "exec"), namespace)
print(namespace["calculate"](3))
