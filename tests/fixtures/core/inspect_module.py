import inspect
import math


def f(x):
    return x + 1


class C:
    cls = 3

    def m(self):
        return 7


c = C()
c.name = "c"

print(inspect.isfunction(f))
print(inspect.isbuiltin(math.sin))
print(inspect.isclass(C))
print(inspect.ismodule(math))
print(inspect.ismethod(c.m))
print(inspect.isroutine(c.m))
print(inspect.currentframe())
print(inspect.stack())

members = inspect.getmembers(c)
print(members[0][0])
methods = inspect.getmembers(C, inspect.isfunction)
print(methods[0][0])
print(inspect.getfile(math))
