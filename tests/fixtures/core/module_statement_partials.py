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

fn_t = identity.__type_params__[0]
box_t = Box.__type_params__[0]
print(fn_t.__name__, fn_t.__bound__ is None, fn_t.__default__ is None)
print(box_t.__name__, type(box_t).__name__)
