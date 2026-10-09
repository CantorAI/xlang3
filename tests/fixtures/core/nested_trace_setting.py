# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Thread trace changes must survive native-to-Python callback return."""
import sys


events = []


def marker(value):
    return value + 1


marker_code = marker.__code__


def first_trace(frame, event, arg):
    if frame.f_code is marker_code and event in ("call", "return"):
        events.append(("first", event))
    return first_trace


def second_trace(frame, event, arg):
    if frame.f_code is marker_code and event in ("call", "return"):
        events.append(("second", event))
    return second_trace


def install(value):
    sys.settrace(first_trace)
    assert sys.gettrace() is first_trace
    return value


try:
    assert sorted([1], key=install) == [1]
    assert sys.gettrace() is first_trace
    assert marker(1) == 2
finally:
    sys.settrace(None)
assert events == [("first", "call"), ("first", "return")]
print("nested callback installs persistent trace")


def replace(value):
    sys.settrace(second_trace)
    assert sys.gettrace() is second_trace
    return value


events.clear()
try:
    sys.settrace(first_trace)
    assert sorted([1], key=replace) == [1]
    assert sys.gettrace() is second_trace
    assert marker(2) == 3
finally:
    sys.settrace(None)
assert events == [("second", "call"), ("second", "return")]
print("nested callback replaces persistent trace")


def disable(value):
    sys.settrace(None)
    assert sys.gettrace() is None
    return value


events.clear()
try:
    sys.settrace(first_trace)
    assert sorted([1], key=disable) == [1]
    assert sys.gettrace() is None
    assert marker(3) == 4
finally:
    sys.settrace(None)
assert events == []
print("nested callback disables persistent trace")
