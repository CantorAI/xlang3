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

print(identity.__type_params__)
print(Box.__type_params__)
