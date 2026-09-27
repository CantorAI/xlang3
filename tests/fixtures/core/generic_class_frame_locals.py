import sys


def inspect_method(method):
    frame = sys._getframe(1)
    names = ("__module__", "__qualname__", "__type_params__", "T")
    print(frame.f_code.co_name)
    print(sorted(name for name in frame.f_locals if name in names))
    print(tuple(param.__name__ for param in frame.f_locals["__type_params__"]))
    return method


class A[T]:
    print(sorted(set(locals()) & {"__module__", "__qualname__", "__type_params__", "T"}))

    @inspect_method
    def f(self, value: T) -> T:
        return value


print(A[int]().f(7))
