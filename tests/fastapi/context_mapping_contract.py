"""Context mapping views and lookup behavior compared with CPython 3.14."""

from contextvars import ContextVar, copy_context


first = ContextVar("first")
second = ContextVar("second")
third = ContextVar("third")
first.set(10)
second.set(20)
context = copy_context()

keys = context.keys()
print(type(keys).__name__, len(keys), sorted(var.name for var in keys))
print(list(keys))
print(type(iter(context)).__name__, sorted(var.name for var in context))
print(type(context.items()).__name__, sorted((var.name, value) for var, value in context.items()))
print(type(context.values()).__name__, sorted(context.values()))
print(first in context, third in context, context.get(first), context.get(third), context.get(third, 99))

snapshot = context.keys()
context.run(third.set, 30)
print(len(snapshot), sorted(var.name for var in snapshot), len(context.keys()))
