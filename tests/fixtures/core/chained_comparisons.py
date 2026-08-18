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

def mark(x):
    print("mark", x)
    return x

print(1 < 2 < 3)
print(1 < 2 > 3)
print(3 > 2 >= 2 == 2)
print(1 < mark(2) < 3)
print(3 < mark(2) < mark(99))
print(1 < 2 in [2, 3])
print(1 < 2 is True)

a = []
b = a
c = []
print(a is b is not c)
