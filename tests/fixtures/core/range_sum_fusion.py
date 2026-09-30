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

def main():
    total = 0
    for item in range(4):
        total = total + item
    print(total)

    total = 0
    for item in range(0):
        total = total + item
    print(total)

    total = 0.5
    for item in range(3):
        total = total + item
    print(total)

    total = 9223372036854775807
    for item in range(2):
        total = total + item
    print(total)

    total = 0
    for item in range(65):
        total = total + item
    print(total, item)

    class Addable:
        def __init__(self, value):
            self.value = value

        def __add__(self, other):
            return Addable(self.value + other)

    total = Addable(0)
    for item in range(3):
        total = total + item
    print(total.value)

main()
