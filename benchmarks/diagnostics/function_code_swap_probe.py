"""Correctness probe for NetworkX's lazy function-code replacement."""


def original(value):
    return "old", value


def replacement(value):
    return "new", value


def cached_call():
    return original(1)


print("before:", cached_call())
original.__code__ = replacement.__code__
print("cached after:", cached_call())
print("new site after:", original(2))
