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
