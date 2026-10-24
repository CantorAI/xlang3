"""Thread profile changes must survive native-to-Python callback return."""
import sys


events = []


def marker(value):
    return value + 1


marker_code = marker.__code__


def first_profile(frame, event, arg):
    if frame.f_code is marker_code and event in ("call", "return"):
        events.append(("first", event))


def second_profile(frame, event, arg):
    if frame.f_code is marker_code and event in ("call", "return"):
        events.append(("second", event))


def install(value):
    sys.setprofile(first_profile)
    assert sys.getprofile() is first_profile
    return value


try:
    assert sorted([1], key=install) == [1]
    assert sys.getprofile() is first_profile
    assert marker(1) == 2
finally:
    sys.setprofile(None)
assert events == [("first", "call"), ("first", "return")]
print("nested callback installs persistent profile")


def replace(value):
    sys.setprofile(second_profile)
    assert sys.getprofile() is second_profile
    return value


events.clear()
try:
    sys.setprofile(first_profile)
    assert sorted([1], key=replace) == [1]
    assert sys.getprofile() is second_profile
    assert marker(2) == 3
finally:
    sys.setprofile(None)
assert events == [("second", "call"), ("second", "return")]
print("nested callback replaces persistent profile")


def disable(value):
    sys.setprofile(None)
    assert sys.getprofile() is None
    return value


events.clear()
try:
    sys.setprofile(first_profile)
    assert sorted([1], key=disable) == [1]
    assert sys.getprofile() is None
    assert marker(3) == 4
finally:
    sys.setprofile(None)
assert events == []
print("nested callback disables persistent profile")
