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


def tracer(frame, event, arg):
    if frame.f_code.co_name == "probe":
        if event == "return":
            events.append(event + ":" + str(arg))
        else:
            events.append(event + ":" + str(frame.f_lineno))
    return tracer


def probe(x):
    y = x + 1
    z = y + 2
    return z


sys.settrace(tracer)
print(probe(3))
sys.settrace(None)
print(events)
