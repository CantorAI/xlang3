import sys
from types import ModuleType


lazy_module = ModuleType("xlang3_lazy_call_fixture_module")
lookups = []


def module_getattr(name):
    lookups.append(name)
    if name == "combine":
        return lambda left, right: left + right
    raise AttributeError(f"module {lazy_module.__name__!r} has no attribute {name!r}")


lazy_module.__getattr__ = module_getattr
sys.modules[lazy_module.__name__] = lazy_module
import xlang3_lazy_call_fixture_module as lazy


def call_lazy():
    return lazy.combine(2, 5)


print("first", call_lazy())
print("second", call_lazy())
print("lookups", lookups)
