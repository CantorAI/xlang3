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

def plain(a, b=2, *, c=3):
    return a + b + c

print(plain.__name__)
print(plain.__qualname__)
print(plain.__defaults__)
print(plain.__kwdefaults__["c"])
print(plain(1))

plain.__defaults__ = (10,)
plain.__kwdefaults__ = {"c": 20}
print(plain(1))

plain.custom = "ok"
print(plain.custom)

def outer():
    def inner():
        return inner.__qualname__
    return inner()

print(outer.__qualname__)
print(outer())

class Box:
    def method(self):
        return Box.method.__qualname__

    class Nested:
        def method(self):
            return Box.Nested.method.__qualname__

print(Box.method.__qualname__)
print(Box().method())
print(Box.Nested.method.__qualname__)
print(Box.Nested().method())

def annotated(x: "int") -> "int":
    return x

annotated.__annotations__ = {"x": "number", "return": "number"}
print(annotated.__annotations__["x"], annotated.__annotations__["return"])
