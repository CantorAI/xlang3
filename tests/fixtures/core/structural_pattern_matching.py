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

value = 10
match value:
    case captured:
        print("capture", captured)

pair = [1, "two"]
match pair:
    case [a, b]:
        print("list", a, b)
    case _:
        print("bad")

point = (3, 4)
match point:
    case (x, y) as whole if x == 3:
        print("tuple", x, y, whole)
    case _:
        print("bad")

record = {"name": "Ada", "age": 7, "extra": True}
match record:
    case {"name": name, "age": 7}:
        print("dict", name)
    case _:
        print("bad")

word = "go"
match word:
    case "stop" | "go":
        print("or")
    case _:
        print("bad")

match [1, 2, 3]:
    case [1, 2]:
        print("bad")
    case _:
        print("length")

match {"name": "missing"}:
    case {"age": age}:
        print("bad", age)
    case _:
        print("missing")
