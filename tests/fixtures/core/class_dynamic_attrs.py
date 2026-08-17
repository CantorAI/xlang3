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
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def total(self):
        return self.x + self.y


def other_total(self):
    return 99


def sum_total(self):
    return self.x + self.y


def other_init(self, x, y):
    self.x = 10
    self.y = 20


p = Point(2, 3)
print(p.total())

Point.total = other_total
print(p.total())

Point.total = sum_total
q = Point(1, 1)
print(q.total())

Point.__init__ = other_init
r = Point(1, 1)
print(r.total())

class Defaults:
    prop1 = 10

    def get_prop1(self):
        return self.prop1

a = Defaults()
b = Defaults()
print(a.prop1, b.prop1, a.get_prop1())
Defaults.prop1 = 20
print(a.prop1, b.prop1, a.get_prop1())
a.prop1 = 99
print(a.prop1, b.prop1, Defaults.prop1)
Defaults.prop1 = 30
print(a.prop1, b.prop1, Defaults.prop1)

class Child(Defaults):
    pass

c = Child()
print(c.prop1)
Child.prop1 = 40
print(c.prop1, b.prop1)
