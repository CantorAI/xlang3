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

x = 99
d = {x: x * x for x in range(6) if x % 2 == 0}
print(d[0], d[2], d[4], x)

s = {x + 1 for x in range(5) if x > 1}
print(len(s), 3 in s, 5 in s, 2 in s, x)

letters = {ch: ch.upper() for ch in "ab"}
print(letters["a"], letters["b"])
