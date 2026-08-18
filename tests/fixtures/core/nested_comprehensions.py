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

x = 100
y = 200

print([x * 10 + y for x in range(3) for y in range(2)])
print([x * 10 + y for x in range(4) if x % 2 == 0 for y in range(3) if y > 0])

d = {x * 10 + y: x + y for x in range(3) for y in range(2)}
print(d[0], d[1], d[10], d[21], x, y)

s = {x + y for x in range(3) for y in range(3) if x != y}
print(len(s), 1 in s, 3 in s, 4 in s, x, y)
