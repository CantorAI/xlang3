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

a, b = [1, 2]
print(a, b)

[c, d] = (3, 4)
print(c, d)

e, *rest = [5, 6, 7, 8]
print(e, rest)

first, *middle, last = (9, 10, 11, 12)
print(first, middle, last)

(x, (y, z)) = [13, [14, 15]]
print(x, y, z)

items = [0, 0]
items[0], items[1] = [21, 22]
print(items)

class Box:
    pass

box = Box()
box.left, box.right = [31, 32]
print(box.left, box.right)
