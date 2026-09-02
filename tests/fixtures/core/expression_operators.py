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

print(7 // 3, -7 // 3, 7 // -3, -7 // -3)
print(2 ** 5, 2 ** 3 ** 2, -2 ** 2, (-2) ** 2)
print(6 & 3, 6 | 3, 6 ^ 3, 1 << 5, 32 >> 2, ~5)
print(3 in [1, 2, 3], 4 not in [1, 2, 3])
print("ell" in "hello", "z" not in "hello")
print("a" in {"a": 1}, "z" not in {"a": 1})
print(2 in {1, 2, 3}, 5 not in {1, 2, 3})
print(4 in range(1, 8, 3), 5 not in range(1, 8, 3))

x = []
y = x
z = []
print(x is y, x is not z, None is None, True is True)

print("yes" if 3 > 2 else "no")
print("left" if False else "right")
print(10 + (1 if False else 2) * 3)
print(.05, 1e-1 + .2)
