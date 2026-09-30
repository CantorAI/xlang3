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

class Pair:
    def __init__(self, reverse):
        if reverse:
            self.y = 20
            self.x = 10
        else:
            self.x = 10
            self.y = 20

    def total(self):
        return self.x + self.y


def total(value):
    return value.total()


print(total(Pair(False)))
print(total(Pair(True)))
mapped = Pair(False)
values = vars(mapped)
values["x"] = 30
print(total(mapped))


class DescriptorPair:
    def __init__(self):
        self._x = 5
        self.y = 8

    @property
    def x(self):
        return self._x + 100

    def total(self):
        return self.x + self.y


print(total(DescriptorPair()))


class HookPair:
    def __init__(self):
        self.x = 1
        self.y = 2

    def __getattribute__(self, name):
        if name == "x":
            return 100
        if name == "y":
            return 200
        return object.__getattribute__(self, name)

    def total(self):
        return self.x + self.y


print(total(HookPair()))


class Box:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return self.value + other.value


class BoxPair:
    def __init__(self):
        self.x = Box(2)
        self.y = Box(3)

    def total(self):
        return self.x + self.y


print(total(BoxPair()))
