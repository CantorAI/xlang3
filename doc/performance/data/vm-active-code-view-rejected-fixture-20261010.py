"""Active IR remains stable when a callable selects new code for later calls."""
import sys


def later():
    return 99


def running():
    running.__code__ = later.__code__
    total = 0
    for value in range(7):
        total += value
    return total, running()


assert running() == (21, 99)
assert running() == 99
print("active-code-self-replacement PASS")


def replacement_generator():
    yield "new"


def original_generator():
    yield "first"
    original_generator.__code__ = replacement_generator.__code__
    yield "second"
    yield "third"


paused = original_generator()
assert next(paused) == "first"
assert next(paused) == "second"
assert next(paused) == "third"
try:
    next(paused)
except StopIteration:
    pass
else:
    raise AssertionError("original generator did not finish")
assert list(original_generator()) == ["new"]
print("active-code-generator-replacement PASS")


def replacement_observed():
    return "new observed"


def observed():
    return "old observed"


old_code = observed.__code__
events = []


def observer(frame, event, argument):
    if frame.f_code is old_code and event == "call":
        events.append(event)
        observed.__code__ = replacement_observed.__code__
    return observer


sys.settrace(observer)
try:
    assert observed() == "old observed"
finally:
    sys.settrace(None)
assert events == ["call"]
assert observed() == "new observed"
print("active-code-trace-replacement PASS")
