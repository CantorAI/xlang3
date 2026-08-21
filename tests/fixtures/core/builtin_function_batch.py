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

print(list(enumerate(["a", "b"], 3)))
print(list(zip([1, 2, 3], ["a", "b"])))
print(sum([1, 2, 3]))
print(sum([1.5, 2.5], 1))
print(min([5, 2, 9]))
print(max(5, 2, 9))
print(abs(-7))
print(abs(-2.5))
print(round(2.6))
print(round(2.25, 1))

def square(x):
    return x * x

def add(a, b):
    return a + b

def is_odd(x):
    return x % 2 == 1

print(callable(square))
print(callable(12))
print(list(map(square, [1, 2, 3])))
print(list(map(add, [1, 2, 3], [10, 20])))
print(list(filter(is_odd, [1, 2, 3, 4])))
print(list(filter(None, [0, 1, "", "x"])))

it = enumerate(["z"], 8)
print(next(it))
print(list(it))

seen = 0

def tap(x):
    global seen
    seen = seen + 1
    return x + 1

lazy = map(tap, [4, 5])
print(seen)
print(next(lazy))
print(seen)
print(list(lazy))

g_value = 42

def read_global_snapshot():
    return globals()["g_value"]

print(globals()["g_value"])
print(read_global_snapshot())

class Box:
    def add(self, x):
        return x + 10

box = Box()
setattr(box, "name", "xlang3")
print(getattr(box, "name"))
print(getattr(box, "missing", "fallback"))
print(hasattr(box, "name"))
print(hasattr(box, "missing"))
print("name" in dir(box))
print(vars(box)["name"])
print(list(map(box.add, [1, 2])))

try:
    getattr(box, "missing")
except AttributeError as err:
    print(type(err).__name__)

try:
    min([])
except ValueError as err:
    print(type(err).__name__)
