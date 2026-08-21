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

d = {"a": 1, "b": 2}
keys = d.keys()
values = d.values()
items = d.items()

print(list(keys))
print(list(values))
print(list(items))
print(len(keys))
print("a" in keys)
print(2 in values)
print(("b", 2) in items)

d["c"] = 3

print(list(keys))
print(len(items))

it = iter(items)
print(next(it))
print(next(it))
print(next(it))
print(next(it, "done"))
