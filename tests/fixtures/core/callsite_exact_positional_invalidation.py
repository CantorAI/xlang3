"""Exercise cached ordinary CALL targets and their invalidation rules."""


def add(left, right):
    return left + right


def multiply(left, right):
    return left * right


def invoke(function, left, right):
    return function(left, right)


# Warm one exact-arity call site before mutating its target and then rebinding it.
for _ in range(4):
    assert invoke(add, 3, 4) == 7

# Replacing code on the original function must invalidate the cached proof.
add.__code__ = multiply.__code__
assert invoke(add, 3, 4) == 12
assert invoke(multiply, 3, 4) == 12

# An already-warm exact call site still creates observable Python frames.
import sys

trace_events = []


def record_call_events(frame, event, arg):
    if event in ("call", "return") and frame.f_code.co_name in ("invoke", "multiply"):
        trace_events.append((event, frame.f_code.co_name))
    return record_call_events


sys.settrace(record_call_events)
try:
    assert invoke(multiply, 4, 5) == 20
finally:
    sys.settrace(None)
assert ("call", "invoke") in trace_events
assert ("call", "multiply") in trace_events
assert ("return", "multiply") in trace_events
assert ("return", "invoke") in trace_events


def with_default(value, increment=9):
    return value + increment


def invoke_one(function, value):
    return function(value)


# A missing positional argument cannot use the exact-arity frame entry.
assert invoke_one(with_default, 5) == 14


def variadic(first, *rest):
    return first, rest


# Variadic signatures retain normal Python argument binding.
assert invoke(variadic, 1, 2) == (1, (2,))


def generate(value):
    yield value


# Generator calls still construct a generator instead of entering its body.
assert next(invoke_one(generate, 23)) == 23

print("ordinary call cache invalidation passed")
