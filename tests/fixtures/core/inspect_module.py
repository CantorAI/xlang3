import inspect
import functools
import math
import os


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
try:
    inspect.getfile(math)
except TypeError as e:
    print(type(e).__name__, "built-in module" in str(e))


def documented(a, b=3):
    """hello
       world"""
    return a + b


@functools.wraps(documented)
def documented_wrapper(a, b=3):
    return documented(a, b)


class Base:
    base_prop = 1


class Derived(Base):
    pass


def gen_func():
    yield 1


async def coro_func():
    return 1


# doc/source helpers.
print(inspect.getdoc(documented))
print(inspect.cleandoc("  a\n    b"))
print(inspect.getmodulename("abc.py"), os.path.abspath(__file__).endswith("inspect_module.py"))
print(len(inspect.getsource(documented)) > 0, inspect.getsourcelines(documented)[1] > 0)

# signature and binding foundations.
signature = inspect.signature(documented)
bound = signature.bind(2)
print(list(signature.parameters.keys()), signature.parameters["b"].default, bound.arguments["a"])
print(signature.bind_partial().arguments)

# unwrap/MRO and generator/coroutine predicates.
print(inspect.unwrap(documented_wrapper) is documented)
print(inspect.getmro(Derived)[0] is Derived, inspect.getmro(Derived)[1] is Base)
g = gen_func()
print(inspect.isgeneratorfunction(gen_func), inspect.isgenerator(g), inspect.getgeneratorstate(g))
co = coro_func()
print(inspect.iscoroutinefunction(coro_func), inspect.iscoroutine(co), inspect.isawaitable(co), inspect.getcoroutinestate(co))
