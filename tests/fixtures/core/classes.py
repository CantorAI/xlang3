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
    kind = "point"

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def total(self):
        return self.x + self.y

    def move(self, dx):
        self.x = self.x + dx
        return self.x

p = Point(2, 3)
print(Point.kind)
print(p.x)
print(p.total())
print(p.move(5))
print(p.total())
p.label = "A"
print(p.label)
