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

class Point:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def total(self):
        return self.x + self.y


p = Point(2, 3)
print(p.total())
p.x = 10
print(p.total())
try:
    p.z = 99
except Exception:
    print("blocked")

del p.x
try:
    print(p.x)
except Exception:
    print("deleted")

class OpenPoint(Point):
    pass


o = OpenPoint(1, 2)
o.z = 7
print(o.total(), o.z)

class NamedPoint(Point):
    __slots__ = "name"

    def set_name(self, name):
        self.name = name


n = NamedPoint(4, 5)
n.set_name("p")
print(n.total(), n.name)
try:
    n.other = 1
except Exception:
    print("named-blocked")

class WithDict:
    __slots__ = ("x", "__dict__")


d = WithDict()
d.x = 1
d.extra = 2
print(d.x, d.extra)
