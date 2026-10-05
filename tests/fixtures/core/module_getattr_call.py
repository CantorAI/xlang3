import sys
from types import ModuleType


lazy_module = ModuleType("xlang3_lazy_call_fixture_module")
lookups = []


def module_getattr(name):
    lookups.append(name)
    if name == "combine":
        return lambda left, right: left + right
    if name == "constructor":
        return LazyConstructor
    if name == "callable":
        return LazyCallable()
    if name == "broken":
        raise ValueError("lazy export failed")
    raise AttributeError(f"module {lazy_module.__name__!r} has no attribute {name!r}")


class LazyConstructor:
    def __init__(self, *, value):
        self.value = value


class LazyCallable:
    def __call__(self, *, left, right):
        return left * right


lazy_module.__getattr__ = module_getattr
sys.modules[lazy_module.__name__] = lazy_module
import xlang3_lazy_call_fixture_module as lazy


def call_lazy():
    return lazy.combine(2, 5)


print("first", call_lazy())
print("second", call_lazy())
print("lookups", lookups)


def call_lazy_keywords():
    return lazy.combine(left=4, right=6)


print("keywords-first", call_lazy_keywords())
print("keywords-second", call_lazy_keywords())
print("constructor", lazy.constructor(value=12).value)
print("callable", lazy.callable(left=3, right=5))
try:
    lazy.combine(unknown=1)
except TypeError:
    print("keyword-error", True)
try:
    lazy.missing(value=1)
except AttributeError:
    print("missing", True)
try:
    lazy.broken(value=1)
except ValueError as exc:
    print("lookup-error", str(exc))
print("keyword-lookups", lookups[2:])

# Native module objects also support PEP 562. A positional call is fused to
# CallMethod, so it must resolve the module hook on an indexed-name miss.
import math
def math_lazy_getattr(name):
    lookups.append("math." + name)
    if name == "lazy_add":
        return lambda value: value + 2
    raise AttributeError(name)
math.__getattr__ = math_lazy_getattr
print("module-call-lazy", math.lazy_add(41), lookups[-1])

import asyncio
from functools import singledispatch

@singledispatch
def future_dispatch_default(value):
    return "object-default"

print("future-mro", [base.__name__ for base in asyncio.Future.__mro__])
print("task-mro", [base.__name__ for base in asyncio.Task.__mro__])
print("future-dispatch", future_dispatch_default.dispatch(asyncio.Future)(None))

import concurrent.futures as futures
with futures.ThreadPoolExecutor(max_workers=1) as executor:
    print("threadpool", executor.submit(lambda: 11).result(timeout=5))
