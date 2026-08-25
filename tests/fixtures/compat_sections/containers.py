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

# Tuple equality/order, count, and index.
t = (1, 2, 2, 4)
print(t.count(2), t.index(2), (1, 2) < (1, 3), (1, 2) == (1, 2))

# List copy/count/reverse/sort and mutation methods.
items = [3, 1, 2, 1]
copy = items.copy()
items.sort()
print(items, copy, items.count(1), items.index(2))
items.sort(reverse=True)
print(items)
items.reverse()
items.remove(1)
items.insert(1, 9)
print(items, items.pop(), items)

# Dict live views, copy, setdefault, pop, popitem, update, and membership.
d = {"a": 1, "b": 2}
keys = d.keys()
values = d.values()
items_view = d.items()
d["c"] = 3
print("c" in keys, 3 in values, ("a", 1) in items_view)
clone = d.copy()
print(clone.get("missing", 7), clone.setdefault("d", 4), clone["d"])
print(clone.pop("a"), "a" in clone)
popped = clone.popitem()
print(len(popped), popped[0] in ("b", "c", "d"))
clone.update({"z": 26})
print(clone["z"], len(clone))
pairs = [("p", 1), ["q", 2]]
clone.update(pairs, r=3)
print(clone["p"], clone["q"], clone["r"])
from_keys = dict.fromkeys(["x", "y"], 5)
print(from_keys["x"], from_keys["y"])
merged = {"a": 1} | {"a": 2, "b": 3}
print(merged["a"], merged["b"])
merged |= {"c": 4}
print(merged["a"], merged["c"])
other_keys = {"b", "c", "d"}
print(keys == {"a", "b", "c"}, keys != {"a", "b"}, keys <= {"a", "b", "c", "d"}, keys < {"a", "b", "c", "d"})
print(keys | {"d"}, keys & other_keys, keys - {"b"}, keys ^ {"a", "d"})
print(items_view == {("a", 1), ("b", 2), ("c", 3)}, items_view & {("a", 1), ("z", 9)})

# Set uniqueness, update/union/copy/pop/clear, and mutation methods.
s = {1, 2, 2}
s.add(3)
s.discard(9)
s.update([3, 4], (5,))
u = s.union({5, 6}, [7])
print(len(s), 4 in s, len(u), 7 in u)
print(s.intersection({2, 4, 9}), s.difference({1, 5}))
print(s | {9}, s & {2, 9}, s - {1, 2}, s ^ {2, 8})
print(s.issubset(u), u.issuperset(s), s.isdisjoint({8, 9}))
s2 = s.copy()
removed = s2.pop()
print(removed in s, len(s2) + 1 == len(s))
s2.remove(1)
s2.update([9])
s2.intersection_update({4, 9})
print(s2)
s2.symmetric_difference_update({4, 10})
print(s2)
s2.difference_update({10})
s2.clear()
print(len(s2))

# Hashability policy for immutable vs mutable container keys.
hash_dict = {(1, 2): "tuple-key", "s": 9}
print(hash_dict[(1, 2)], hash_dict["s"])
try:
    bad = {[1]: "bad"}
except Exception:
    print("list-unhashable")
