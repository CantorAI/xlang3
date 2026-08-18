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

x = 10
x += 5
x -= 3
x *= 4
x //= 6
x %= 5
print(x)

y = 2
y **= 5
y >>= 2
y <<= 3
y |= 3
y &= 14
y ^= 6
print(y)

z = 7
z /= 2
print(z)

items = [1, 2, 3]
items[1] += 20
print(items)

class Box:
    def __init__(self):
        self.value = 4

box = Box()
box.value *= 5
print(box.value)
