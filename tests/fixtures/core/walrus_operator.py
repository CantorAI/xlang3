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

values = [1, 2, 3, 4]

if n := len(values):
    print("len", n)

total = 0
while (item := values.pop()) > 2:
    total += item

print(total, len(values))
print([(y := x * 2) for x in range(4) if (y := x * 2) > 2])
print(y)

def f():
    if m := 5:
        print(m)
    print(m + 1)

f()
