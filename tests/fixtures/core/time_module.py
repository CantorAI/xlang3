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

import time

a = time.monotonic_ns()
time.sleep(0)
b = time.monotonic_ns()
print(b >= a)
print(time.time_ns() > 0)
print(time.perf_counter() >= 0)
epoch = time.gmtime(0)
print(isinstance(epoch, time.struct_time), epoch.tm_year, epoch.tm_mon, epoch.tm_mday, epoch.tm_wday, epoch.tm_yday, epoch.tm_isdst)
print(time.strftime("%Y-%m-%d %H:%M:%S", epoch))
print(time.asctime(epoch).endswith("1970"), isinstance(time.ctime(0), str))
print(time.mktime(time.localtime(0)) == 0.0)
parsed = time.strptime("2024-02-29 11 pm", "%Y-%m-%d %I %P")
print(parsed.tm_year, parsed.tm_mon, parsed.tm_mday, parsed.tm_hour, parsed.tm_yday, parsed.tm_wday)
print(repr(parsed))
try:
    time.strptime("2026x", "%Y")
except ValueError as err:
    print("strptime-trailing-diagnostic", str(err) == "unconverted data remains: x")
try:
    time.strptime("2026 54 1", "%Y %U %w")
except ValueError as err:
    print("strptime-mismatch-diagnostic", str(err) == "time data '2026 54 1' does not match format '%Y %U %w'")
for bounded_text, bounded_format, bounded_tail in [
    ("2026 01 32", "%Y %m %d", "2"),
    ("23:60", "%H:%M", "0"),
    ("23:59:62", "%H:%M:%S", "2"),
    ("24", "%k", "4"),
    ("13", "%l", "3"),
]:
    try:
        time.strptime(bounded_text, bounded_format)
    except ValueError as err:
        print("strptime-bounded-diagnostic", bounded_text, str(err) == "unconverted data remains: " + bounded_tail)
