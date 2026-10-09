"""Lazy method annotations use class scope without changing method-body scope."""


def annotation_holder():
    token = object()
    class Annotated:
        target = token
        value: target
        def method(self, arg: target) -> target:
            return arg
    return Annotated


Annotated = annotation_holder()
assert Annotated.__annotations__['value'] is Annotated.target
assert Annotated.method.__annotations__['arg'] is Annotated.target
assert Annotated.method.__annotations__['return'] is Annotated.target
print('class and method lazy annotations retain class captures')


def distinct_scopes():
    target = object()
    class MethodOnly:
        target = object()
        def method(self, arg: target) -> target:
            return target
    return MethodOnly, target


MethodOnly, outer_target = distinct_scopes()
assert MethodOnly.target is not outer_target
assert MethodOnly.method.__annotations__['arg'] is MethodOnly.target
assert MethodOnly.method.__annotations__['return'] is MethodOnly.target
assert MethodOnly.method(None, None) is outer_target
print('method-only annotations capture class names while body retains enclosing scope')
