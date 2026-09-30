import builtins
import sys


def hook(*args, **kwargs):
    print("hook", args, sorted(kwargs.items()))
    return 42


original = sys.breakpointhook
try:
    sys.breakpointhook = hook
    print(builtins.breakpoint is breakpoint)
    print(repr(breakpoint))
    print(breakpoint.__module__, breakpoint.__name__)
    print(breakpoint(3, label="ready"))
finally:
    sys.breakpointhook = original
