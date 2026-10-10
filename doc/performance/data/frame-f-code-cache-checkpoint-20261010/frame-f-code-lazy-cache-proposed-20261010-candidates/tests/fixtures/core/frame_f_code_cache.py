# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Stable repeated reads of one retained frame, without changing its code."""
import gc
import sys


def capture_plain_frame():
    return sys._getframe()


plain_frame = capture_plain_frame()
plain_code = plain_frame.f_code
plain_id = id(plain_code)
assert plain_code.co_name == "capture_plain_frame"
for unused in range(8):
    assert id(plain_frame.f_code) == plain_id
    assert plain_frame.f_code is plain_code
plain_frame.clear()
assert id(plain_frame.f_code) == plain_id
assert plain_frame.f_code.co_name == "capture_plain_frame"
del plain_frame
gc.collect()
assert plain_code.co_name == "capture_plain_frame"
print("retained frame code identity and lifetime")


def active_replacement_body():
    return "new active body"


def active_original_body():
    frame = sys._getframe()
    original_code = frame.f_code
    original_id = id(original_code)
    active_original_body.__code__ = active_replacement_body.__code__
    assert frame.f_code.co_name == "active_original_body"
    assert id(frame.f_code) == original_id
    return "old active body", frame, original_code


active_result, active_frame, active_code = active_original_body()
assert active_result == "old active body"
assert active_original_body() == "new active body"
assert active_frame.f_code.co_name == "active_original_body"
assert id(active_frame.f_code) == id(active_code)
active_frame.clear()
assert id(active_frame.f_code) == id(active_code)
print("active frame retains old code after replacement")


def suspended_original_body():
    frame = sys._getframe()
    original_code = frame.f_code
    yield frame, original_code
    assert id(frame.f_code) == id(original_code)
    yield "old suspended continuation"


def suspended_replacement_body():
    yield "new generator body"


suspended = suspended_original_body()
suspended_frame, suspended_code = next(suspended)
suspended_original_body.__code__ = suspended_replacement_body.__code__
assert id(suspended_frame.f_code) == id(suspended_code)
assert next(suspended) == "old suspended continuation"
try:
    next(suspended)
except StopIteration:
    pass
else:
    assert False, "old generator must finish"
assert next(suspended_original_body()) == "new generator body"
assert suspended_frame.f_code.co_name == "suspended_original_body"
assert id(suspended_frame.f_code) == id(suspended_code)
print("suspended frame retains original code")


trace_frames = []
trace_codes = []
trace_events = []


def observed_marker_for_code_cache(value):
    intermediate = value + 1
    return intermediate


def trace_code_reads(frame, event, arg):
    code = frame.f_code
    if code.co_name == "observed_marker_for_code_cache":
        if not trace_frames:
            trace_frames.append(frame)
            trace_codes.append(code)
        assert id(frame) == id(trace_frames[0])
        assert id(frame.f_code) == id(trace_codes[0])
        if event in ("call", "return"):
            trace_events.append(event)
    return trace_code_reads


previous_trace = sys.gettrace()
try:
    sys.settrace(trace_code_reads)
    assert observed_marker_for_code_cache(41) == 42
finally:
    sys.settrace(previous_trace)
assert trace_events == ["call", "return"]
assert len(trace_frames) == 1
retained_trace_frame, retained_trace_code = trace_frames[0], trace_codes[0]
assert id(retained_trace_frame.f_code) == id(retained_trace_code)
retained_trace_frame.clear()
assert id(retained_trace_frame.f_code) == id(retained_trace_code)
trace_frames.clear()
del retained_trace_frame
gc.collect()
assert retained_trace_code.co_name == "observed_marker_for_code_cache"
print("trace frame code survives callback and frame clear")


def raise_for_code_cache():
    raise ValueError("frame code cache traceback")


failed_frame = None
try:
    raise_for_code_cache()
except ValueError as error:
    traceback = error.__traceback__
    while traceback is not None:
        candidate = traceback.tb_frame
        if candidate.f_code.co_name == "raise_for_code_cache":
            failed_frame = candidate
            break
        traceback = traceback.tb_next
assert failed_frame is not None
failed_code = failed_frame.f_code
assert id(failed_frame.f_code) == id(failed_code)
failed_frame.clear()
assert id(failed_frame.f_code) == id(failed_code)
assert failed_code.co_name == "raise_for_code_cache"
print("traceback frame code remains stable after clear")
