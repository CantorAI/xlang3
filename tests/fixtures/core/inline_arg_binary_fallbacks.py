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

def add3(a, b, c):
    return a + b + c


class Addable:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return Addable(self.value + other)


# Warm the same call site so the tests below exercise its cached inline path.
print(add3(1, 2, 3))
print(add3(9223372036854775807, 1, 2))
print(add3(Addable(10), 2, 3).value)
