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


monitoring = sys.monitoring
events = monitoring.events


def target():
    return None


for tool_id in (3, 4):
    try:
        monitoring.free_tool_id(tool_id)
    except ValueError:
        pass

print("monitoring-all-events-empty", monitoring._all_events() == {})
print(monitoring.use_tool_id(3, "all-events-a") is None)
print(monitoring.use_tool_id(4, "all-events-b") is None)
print("monitoring-all-events-used", monitoring._all_events() == {})
monitoring.set_events(3, events.PY_RETURN | events.LINE)
monitoring.set_events(4, events.LINE | events.CALL)
all_events = monitoring._all_events()
print(
    "monitoring-all-events-global",
    all_events.get("PY_RETURN"),
    all_events.get("CALL"),
    all_events.get("LINE"),
    "PY_START" in all_events,
)
monitoring.set_local_events(3, target.__code__, events.PY_START)
local_ignored = monitoring._all_events()
print(
    "monitoring-all-events-local-ignored",
    local_ignored.get("PY_RETURN"),
    local_ignored.get("CALL"),
    local_ignored.get("LINE"),
    "PY_START" in local_ignored,
)
monitoring.set_events(3, 0)
monitoring.set_events(4, 0)
print("monitoring-all-events-cleared", monitoring._all_events() == {})
monitoring.set_local_events(3, target.__code__, 0)
monitoring.free_tool_id(3)
monitoring.free_tool_id(4)


def stop_iteration_generator():
    yield 1
    return 2


def stop_iteration_target():
    total = 0
    for value in stop_iteration_generator():
        total += value
    return total


for _ in range(100):
    stop_iteration_target()
stop_iteration_events = []


def stop_iteration_callback(code, instruction_offset, exception):
    if code.co_name == "stop_iteration_target":
        stop_iteration_events.append((type(exception).__name__, isinstance(instruction_offset, int)))


print(monitoring.use_tool_id(3, "stop-iteration") is None)
print(monitoring.register_callback(3, events.STOP_ITERATION, stop_iteration_callback) is None)
monitoring.set_events(3, events.STOP_ITERATION)
print("monitoring-stop-iteration", stop_iteration_target())
monitoring.set_events(3, 0)
monitoring.free_tool_id(3)
print(stop_iteration_events)


disabled_lines = []
disabled_codes = []


def disabled_line_callback(code, line):
    disabled_codes.append(code)
    disabled_lines.append(line)
    return monitoring.DISABLE


def disabled_line_target():
    first = 1
    second = 2
    return first + second


print(monitoring.use_tool_id(3, "disabled-lines") is None)
print(monitoring.register_callback(3, events.LINE, disabled_line_callback) is None)
monitoring.set_local_events(3, disabled_line_target.__code__, events.LINE)
print("monitoring-disable-first", disabled_line_target(), len(disabled_lines))
print("monitoring-code-identity", all(code is disabled_codes[0] for code in disabled_codes))
disabled_line_count = len(disabled_lines)
print("monitoring-disable-cached", disabled_line_target(), len(disabled_lines) == disabled_line_count)
monitoring.restart_events()
print("monitoring-disable-restarted", disabled_line_target(), len(disabled_lines) > disabled_line_count)
monitoring.free_tool_id(3)
