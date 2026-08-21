# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import sys

events = []


def call_only(frame, event, arg):
    if frame.f_code.co_name == "silent":
        events.append(event)
        return None
    return local_trace


def local_trace(frame, event, arg):
    if frame.f_code.co_name == "boom":
        if event == "exception":
            events.append(event + ":" + str(arg[0].__name__))
        else:
            events.append(event + ":" + str(frame.f_lineno))
    return local_trace


def silent():
    a = 1
    b = a + 1
    return b


def boom():
    x = 1
    raise ValueError("bad")


sys.settrace(call_only)
print(silent())
try:
    boom()
except ValueError:
    print("handled")
sys.settrace(None)
print(events)
