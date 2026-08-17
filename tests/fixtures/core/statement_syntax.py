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

from import_helper import *

x = 2
if x == 1:
    print("if")
elif x == 2:
    print("elif")
else:
    print("else")

items = []
for i in range(6):
    if i == 1:
        continue
    if i == 4:
        break
    items.append(i)
print(items)

n = 0
while n < 5:
    n = n + 1
    if n == 2:
        continue
    if n == 4:
        break
print(n)

pass
assert x == 2, "x should be 2"

try:
    y = 10
except Exception:
    print("bad")
else:
    print("try_else", y)
finally:
    print("finally")

class CM:
    def __init__(self, name):
        self.name = name

    def __enter__(self):
        print("enter", self.name)
        return self.name

    def __exit__(self, exc_type, exc_value, tb):
        print("exit", self.name)
        return False

with (
    CM("a") as a,
    CM("b") as b,
):
    print("with", a, b)

d = {"a": 1, "b": 2}
del d["a"]
print(d["b"])

obj = CM("obj")
obj.extra = 99
del obj.extra
print("del_attr")

match x:
    case 1:
        print("one")
    case 2:
        print("two")
    case _:
        print("wild")

print(star_value)
