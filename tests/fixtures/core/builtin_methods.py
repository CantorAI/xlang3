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

items = [1, 2]
items.append(3)
print(items)
print(items.pop())
print(items)
items.extend([4, 5])
items.insert(1, 9)
print(items)
print(items.pop(1))
items.clear()
print(len(items))

d = {"a": 1}
print(d.get("a"))
print(d.get("missing"))
print(d.get("missing", 9))
print(d.keys())
print(d.values())
print(d.items())
print(d.pop("a"))
print(d.pop("missing", 8))
print(len(d))
d["z"] = 3
d.clear()
print(len(d))

s = {1}
s.add(2)
s.add(2)
print(len(s))
s.discard(2)
s.discard(9)
print(len(s))
s.remove(1)
print(len(s))

print("AbC".lower())
print("AbC".upper())
print("  hi  ".strip())
print("a,b,c".split(","))
print("a b  c".split())
print("abc".startswith("ab"))
