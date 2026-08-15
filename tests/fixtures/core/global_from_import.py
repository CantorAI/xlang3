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

from import_helper import add as plus, read_value
from math import sqrt, pi
import import_helper as ih

count = 0

def inc():
    global count
    count = count + 1
    return count

print(plus(4, 5))
print(read_value())
print(ih.value)
print(sqrt(16))
print(pi > 3)
print(inc())
print(inc())
print(count)
