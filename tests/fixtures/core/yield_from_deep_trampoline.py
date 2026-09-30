def leaf():
    sent = yield "ready"
    return sent + 1


def layer(child):
    return_value = yield from child
    return return_value + 1


generator = leaf()
for _ in range(12):
    generator = layer(generator)
print(next(generator))
try:
    generator.send(10)
except StopIteration as result:
    print("deep-return", result.value)


def failing_leaf():
    yield "before-error"
    raise ValueError("delegated")


def catches_delegated_error():
    try:
        yield from failing_leaf()
    except ValueError:
        yield "caught-error"


catcher = catches_delegated_error()
print(next(catcher), next(catcher))


def unhandled_leaf():
    yield "before-unhandled-error"
    raise RuntimeError("unhandled delegated error")


def unhandled_wrapper(child):
    yield from child


unhandled = unhandled_wrapper(unhandled_leaf())
print(next(unhandled))
try:
    next(unhandled)
except RuntimeError as error:
    print("unhandled", str(error))
try:
    next(unhandled)
except StopIteration:
    print("parents-closed")


trace_events = []


def trace(frame, event, arg):
    if frame.f_code.co_name in ("traced_leaf", "traced_wrapper") and event == "return":
        trace_events.append(frame.f_code.co_name)
    return trace


def traced_leaf():
    yield "trace-value"


def traced_wrapper():
    yield from traced_leaf()


import sys
sys.settrace(trace)
traced = traced_wrapper()
next(traced)
try:
    next(traced)
except StopIteration:
    pass
sys.settrace(None)
print("trace-yields", trace_events.count("traced_wrapper"), trace_events.count("traced_leaf"))
