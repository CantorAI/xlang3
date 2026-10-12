"""Separate protocol checks from the direct and indirect call paths around them.

Diagnostic only. Timings include loop overhead; compare matching call shapes
across runtimes, and do not treat these values as pyperf scores.
"""
import inspect
import statistics
import sys
import time
import typing
import weakref


@typing.runtime_checkable
class HasX(typing.Protocol):
    x: int


@typing.runtime_checkable
class HasMany(typing.Protocol):
    a: int
    b: int
    c: int
    d: int
    e: int


class Empty:
    pass


class Parent:
    pass


class Child(Parent):
    pass


class HasInstanceX:
    def __init__(self):
        self.x = 42


class NominalX(HasX):
    def __init__(self):
        self.x = 42


empty = Empty()
present = HasInstanceX()
nominal = NominalX()
dynamic_noop = lambda value: value
dynamic_isinstance = isinstance
dynamic_static_get = inspect.getattr_static
dynamic_getattr = getattr
dynamic_abc_check = typing._abc_instancecheck
dynamic_protocol_hook = HasX.__instancecheck__
dynamic_weakref = weakref.ref(present)


def python_call_direct(loops):
    for _ in range(loops):
        dynamic_noop(empty)


def python_call_indirect(loops):
    callee = dynamic_noop
    for _ in range(loops):
        callee(empty)


def exact_isinstance_direct(loops):
    for _ in range(loops):
        isinstance(empty, Empty)


def exact_isinstance_indirect(loops):
    callee = dynamic_isinstance
    for _ in range(loops):
        callee(empty, Empty)


def exact_issubclass_direct(loops):
    for _ in range(loops):
        issubclass(Child, Parent)


def exact_issubclass_indirect(loops):
    callee = issubclass
    for _ in range(loops):
        callee(Child, Parent)


def protocol_positive_direct(loops):
    for _ in range(loops):
        isinstance(present, HasX)


def protocol_positive_indirect(loops):
    callee = dynamic_isinstance
    for _ in range(loops):
        callee(present, HasX)


def protocol_negative_direct(loops):
    for _ in range(loops):
        isinstance(empty, HasX)


def protocol_nominal_direct(loops):
    for _ in range(loops):
        isinstance(nominal, HasX)


def protocol_many_negative_direct(loops):
    for _ in range(loops):
        isinstance(empty, HasMany)


def protocol_hook_direct(loops):
    for _ in range(loops):
        HasX.__instancecheck__(present)


def abc_instancecheck_direct(loops):
    for _ in range(loops):
        typing._abc_instancecheck(HasX, type(present))


def static_attribute_direct(loops):
    for _ in range(loops):
        inspect.getattr_static(present, "x")


def ordinary_attribute_direct(loops):
    for _ in range(loops):
        getattr(present, "x")


def weakref_deref_direct(loops):
    for _ in range(loops):
        dynamic_weakref()


CASES = [
    ("python_call_direct", python_call_direct),
    ("python_call_indirect", python_call_indirect),
    ("exact_isinstance_direct", exact_isinstance_direct),
    ("exact_isinstance_indirect", exact_isinstance_indirect),
    ("exact_issubclass_direct", exact_issubclass_direct),
    ("exact_issubclass_indirect", exact_issubclass_indirect),
    ("protocol_positive_direct", protocol_positive_direct),
    ("protocol_positive_indirect", protocol_positive_indirect),
    ("protocol_negative_direct", protocol_negative_direct),
    ("protocol_nominal_direct", protocol_nominal_direct),
    ("protocol_many_negative_direct", protocol_many_negative_direct),
    ("protocol_hook_direct", protocol_hook_direct),
    ("abc_instancecheck_direct", abc_instancecheck_direct),
    ("static_attribute_direct", static_attribute_direct),
    ("ordinary_attribute_direct", ordinary_attribute_direct),
    ("weakref_deref_direct", weakref_deref_direct),
]


def main():
    loops = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    print("case,loops,median_seconds")
    for name, body in CASES:
        body(100)
        samples = []
        for _ in range(5):
            start = time.perf_counter()
            body(loops)
            samples.append(time.perf_counter() - start)
        print(f"{name},{loops},{statistics.median(samples):.9f}")


if __name__ == "__main__":
    main()
