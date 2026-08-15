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
d = {"a": 1, "b": 2, "a": 3}
print(d["a"])
d["c"] = 4
print(d["c"])
print(len(d))

total = 0
for k in d:
    total = total + d[k]
print(total)

s = {1, 2, 2, 3}
print(len(s))
sum = 0
for x in s:
    sum = sum + x
print(sum)

items = [1, 2]
items[1] = 5
print(items)
