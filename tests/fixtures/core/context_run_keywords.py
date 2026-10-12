from contextvars import ContextVar, copy_context

marker = ContextVar("marker", default="outside")
context = copy_context()


def combine(first, *, second):
    marker.set("inside")
    return first + second, marker.get()


print(context.run(combine, 2, second=3))
print(marker.get(), context.run(marker.get))

# Nested runs must restore the active outer context without copying its map.
outer = copy_context()
marker.set("outside nested")
inner = copy_context()


def run_inner():
    before = marker.get()
    inner_value = inner.run(marker.get)
    after = marker.get()
    marker.set("updated outer")
    return before, inner_value, after


print(outer.run(run_inner))
print(marker.get(), outer.run(marker.get))


def fail(*, reason):
    marker.set("after failure")
    raise ValueError(reason)


try:
    context.run(fail, reason="boom")
except ValueError as exc:
    print(type(exc).__name__, str(exc))
print(marker.get(), context.run(marker.get))
