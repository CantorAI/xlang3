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

class A:
    tag = "A"
    def who(self):
        return "A"

class B(A):
    tag = "B"

class C(A):
    def who(self):
        return "C"

class D(B, C):
    pass

class E(C, B):
    pass

d = D()
e = E()

for item in D.__bases__:
    print("base", item.__name__)

for item in D.__mro__:
    print("mro", item.__name__)

print(d.tag, d.who())
print(e.tag, e.who())
print(isinstance(d, A), isinstance(d, B), isinstance(d, C), isinstance(d, object))
print(issubclass(D, A), issubclass(D, C), issubclass(E, B))
