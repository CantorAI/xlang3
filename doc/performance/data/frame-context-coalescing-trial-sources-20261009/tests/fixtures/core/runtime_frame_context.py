import sys


saved = []


def leaf(value):
    if value < 0:
        raise ValueError("negative")
    frame = sys._getframe()
    assert frame.f_code is leaf.__code__
    assert frame.f_locals["value"] == value
    assert frame.f_globals is globals()
    assert frame.f_back.f_code is driver.__code__
    later = value + 1
    saved.append(frame)
    return later


def driver(value):
    if value < 0:
        return 0
    return leaf(value)


assert [driver(value) for value in range(8)] == list(range(1, 9))
assert [frame.f_locals["later"] for frame in saved] == list(range(1, 9))
for frame in saved:
    frame.clear()
saved.clear()
print("PASS same-globals-live-and-escaped-frames")


foreign = {"__builtins__": __builtins__}
exec(compile("def foreign_entry(value, callback):\n"
             "    import sys\n"
             "    frame = sys._getframe()\n"
             "    assert frame.f_globals is globals()\n"
             "    assert frame.f_locals['value'] == value\n"
             "    result = callback(value)\n"
             "    assert sys._getframe() is frame\n"
             "    return result, frame\n", "frame_context_foreign.py", "exec"), foreign)


def callback(value):
    if value < 0:
        raise ValueError("negative")
    frame = sys._getframe()
    assert frame.f_globals is globals()
    assert frame.f_back.f_globals is foreign
    return value + 3


answer, foreign_frame = foreign["foreign_entry"](4, callback)
assert answer == 7 and foreign_frame.f_globals is foreign
assert foreign_frame.f_locals["result"] == 7
foreign_frame.clear()
print("PASS changed-globals-nested-return")


events = []


def observed(value):
    if value < 0:
        return 0
    later = value + 1
    assert sys._getframe().f_locals["later"] == later
    return later


def trace(frame, event, arg):
    if frame.f_code is observed.__code__ and event in ("call", "return"):
        assert frame.f_globals is globals()
        events.append(("trace", event, frame.f_locals["value"]))
    return trace


def profile(frame, event, arg):
    if frame.f_code is observed.__code__ and event in ("call", "return"):
        assert frame.f_globals is globals()
        events.append(("profile", event, frame.f_locals["value"]))


sys.settrace(trace)
sys.setprofile(profile)
try:
    assert observed(10) == 11
finally:
    sys.setprofile(None)
    sys.settrace(None)
assert sorted(events) == sorted([
    ("trace", "call", 10), ("trace", "return", 10),
    ("profile", "call", 10), ("profile", "return", 10)])
print("PASS trace-profile-frame-context")


class Base:
    def choose(self, value):
        if value < 0:
            return 0
        return value + 1


class Override(Base):
    def choose(self, value):
        frame = sys._getframe()
        assert frame.f_locals["self"] is self
        assert frame.f_globals is globals()
        return value + 2


outer = LookupError("outer")


def handled_key(value):
    assert sys.exception() is outer
    try:
        raise KeyError("inner")
    except KeyError as error:
        assert sys.exception() is error and error.__context__ is outer
    assert sys.exception() is outer
    return Override().choose(value)


try:
    raise outer
except LookupError:
    assert sorted([3, 1], key=handled_key) == [1, 3]
    assert sys.exception() is outer
assert Base().choose(3) == 4 and Override().choose(3) == 5
print("PASS overrides-and-handled-context")
