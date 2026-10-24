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
"""Strict CP-first probes for existing frame/function identity and metadata gaps.

These are separate from the lazy per-frame cache's correctness fixture. Each
case is expected to pass on CPython3.14.7; current X behavior must be captured,
not silently accepted or represented as fixed by the cache.
"""
import sys

assert len(sys.argv) == 2
selection = sys.argv[1]


def canonical_capture():
    return sys._getframe()


def override_capture():
    return sys._getframe()


if selection == "canonical":
    frame = canonical_capture()
    canonical_code = canonical_capture.__code__
    assert frame.f_code is canonical_code
    assert id(frame.f_code) == id(canonical_code)
    print("frame uses exact canonical function code object")
elif selection == "assigned_metadata":
    assigned = override_capture.__code__.replace(
        co_filename="frame-code-assigned-override.py",
        co_name="assigned_frame_body", co_qualname="Owner.assigned_frame_body",
        co_firstlineno=701)
    override_capture.__code__ = assigned
    frame = override_capture()
    assert id(override_capture.__code__) == id(assigned)
    assert frame.f_code.co_filename == "frame-code-assigned-override.py"
    assert frame.f_code.co_name == "assigned_frame_body"
    assert frame.f_code.co_qualname == "Owner.assigned_frame_body"
    assert frame.f_code.co_firstlineno == 701
    assert id(frame.f_code) == id(assigned)
    print("frame preserves exact assigned code object and metadata")
else:
    raise AssertionError("unknown separate baseline probe")
