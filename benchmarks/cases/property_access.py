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
    def __init__(self):
        self._value = 1

    @property
    def value(self):
        return self._value + 1

    @value.setter
    def value(self, item):
        self._value = item - 1

    @value.deleter
    def value(self):
        self._value = 1


def main():
    counter = Counter()
    total = 0
    i = 0
    while i < 180000:
        counter.value = i
        total = total + counter.value
        if i % 50000 == 0:
            del counter.value
            total = total + counter.value
        i = i + 1
    print(total)


main()
