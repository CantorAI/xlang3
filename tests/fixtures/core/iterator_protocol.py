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

it = iter([1, 2])
print(next(it))
print(next(it))
print(next(it, "done"))

try:
    next(it)
except StopIteration as err:
    print(type(err).__name__)

z = zip([1], ["a"])
print(iter(z) is z)
print(next(z))
print(next(z, "zip-done"))

def inc(x):
    return x + 1

m = map(inc, [4])
print(iter(m) is m)
print(next(m))
print(next(m, "map-done"))

def greater_than_two(x):
    return x > 2

f = filter(greater_than_two, [1, 3])
print(iter(f) is f)
print(next(f))
print(next(f, "filter-done"))
