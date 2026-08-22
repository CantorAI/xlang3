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

match [1, 2, 3, 4]:
    case [head, *middle, tail]:
        print(head, middle, tail)
    case _:
        print("bad")

match [1]:
    case [head, *middle, tail]:
        print("bad")
    case _:
        print("short")

match ("a", "b", "c"):
    case ("a", *_, last):
        print(last)
    case _:
        print("bad")

def identity[T, U](value):
    return value

class Box[T]:
    pass

class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

p = Point(2, [3, 4])

match p:
    case Point(2, [first, second]):
        print("point-pos", first, second)
    case _:
        print("bad")

match p:
    case Point(x=2, y=yy):
        print("point-kw", yy[0])
    case _:
        print("bad")

dynamic_match_args = ("x", "y")

class DynamicPoint:
    __match_args__ = dynamic_match_args

    def __init__(self, x, y):
        self.x = x
        self.y = y

dp = DynamicPoint(5, 6)

match dp:
    case DynamicPoint(5, dyn_y):
        print("point-dynamic", dyn_y)
    case _:
        print("bad")

failed_capture = "old"

# Failed sequence pattern captures must not overwrite an existing name.
match [1, 3]:
    case [failed_capture, 2]:
        print("bad")
    case _:
        print("capture", failed_capture)

# OR patterns bind from the winning alternative.
match [8, 2]:
    case [or_value, 1] | [or_value, 2]:
        print("or-capture", or_value)
    case _:
        print("bad")

# Wildcard can participate inside an OR pattern.
match 99:
    case _ | 1:
        print("or-wildcard")
    case _:
        print("bad")

fn_t = identity.__type_params__[0]
box_t = Box.__type_params__[0]
print(fn_t.__name__, fn_t.__bound__ is None, fn_t.__default__ is None)
print(box_t.__name__, type(box_t).__name__)
