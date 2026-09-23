from contextvars import ContextVar, copy_context

marker = ContextVar("marker", default="outside")
context = copy_context()


def combine(first, *, second):
    marker.set("inside")
    return first + second, marker.get()


print(context.run(combine, 2, second=3))
print(marker.get(), context.run(marker.get))


def fail(*, reason):
    marker.set("after failure")
    raise ValueError(reason)


try:
    context.run(fail, reason="boom")
except ValueError as exc:
    print(type(exc).__name__, str(exc))
print(marker.get(), context.run(marker.get))
