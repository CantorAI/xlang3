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


def debug_hook(frame, event):
    events.append(event + ":" + frame.f_code.co_name + ":" + str(frame.f_lineno - frame.f_code.co_firstlineno))
    if len(events) >= 4:
        sys._xlang3_debug_continue()


def target():
    x = 1
    y = x + 2
    return y


def run_step():
    sys._xlang3_debug_step_into()
    return target()


sys._xlang3_debug_set_hook(debug_hook)
print(sys._xlang3_debug_poll_needed())
sys._xlang3_debug_add_breakpoint("debug_breakpoint_step.py", target.__code__.co_firstlineno + 2)
print(sys._xlang3_debug_poll_needed())
print(target())
sys._xlang3_debug_clear_breakpoints()
print(sys._xlang3_debug_poll_needed())
run_step()
print(sys._xlang3_debug_poll_needed())
for item in events:
    print(item)
sys._xlang3_debug_set_hook(None)
print(sys._xlang3_debug_poll_needed())
