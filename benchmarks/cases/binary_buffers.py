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
    data = bytearray(b"abcdefghijklmnopqrstuvwxyz")
    view = memoryview(data)
    total = 0
    i = 0
    while i < 220000:
        index = i % 26
        data[index] = (data[index] + i) % 256
        total = total + view[index]
        i = i + 1
    frozen = bytes(data)
    j = 0
    while j < len(frozen):
        total = total + frozen[j]
        j = j + 1
    print(total)


main()
