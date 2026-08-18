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

class Counter:
    def __init__(self, value):
        self.value = value

    def inc(self):
        self.value = self.value + 1
        return self

    def add(self, value):
        return Counter(self.value + value)

    def get(self):
        return self.value

class Box:
    def __init__(self, text):
        self.text = text

    def wrap(self, left, right):
        return Box(left + self.text + right)

    def upper(self):
        return Box(self.text.upper())

    def get(self):
        return self.text

class TextPipe:
    def __init__(self, text):
        self.text = text

    def clean(self):
        return TextPipe(self.text.strip())

    def swap(self):
        return TextPipe(self.text.replace("a", "A"))

    def cut(self):
        return TextPipe(self.text[1:4])

    def get(self):
        return self.text

class Bag:
    def __init__(self, items):
        self.items = items

    def middle(self):
        return Bag(self.items[1:4])

    def push(self, value):
        self.items.append(value)
        return self

    def at(self, index):
        return self.items[index]

c = Counter(1)
print(c.inc().inc().get())
print(c.get())
print(Counter(5).add(7).add(9).get())
print(Box("x").wrap("[", "]").upper().wrap("<", ">").get())
print(TextPipe("  catapult  ").clean().swap().cut().get())
print(Bag([10, 20, 30, 40, 50]).middle().push(99).at(3))
