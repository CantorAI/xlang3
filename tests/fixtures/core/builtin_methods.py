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

def exercise_cached_list_methods():
    values = []
    insert = values.insert
    pop = values.pop
    for item in range(6):
        insert(0, item)
    print(values)
    for _ in range(3):
        print(pop(0))
    print(values)

exercise_cached_list_methods()

def rotate_with_cached_methods(values, index):
    insert = values.insert
    pop = values.pop
    insert(index, pop(0))
    return values

print(rotate_with_cached_methods([0, 1, 2, 3, 4], 2))
print(rotate_with_cached_methods([0, 1, 2], True))
bool_indexed = [0, 1, 2]
bool_indexed.insert(True, 9)
print(bool_indexed)
print(bool_indexed.pop(True))

def rotate_from_different_list(values, source, index):
    insert = values.insert
    pop = source.pop
    insert(index, pop(0))
    return values, source

print(rotate_from_different_list([0, 1, 2], [9], 1))

def reset_counts(counts, index):
    while index != 1:
        counts[index - 1] = index
        index -= 1
    return counts, index

print(reset_counts([0, 0, 0, 0], 4))
print(reset_counts([0, 0], True))
try:
    reset_counts([0, 0], 4)
except Exception as error:
    print(type(error).__name__)

def compare_list_item(values, index, rhs):
    return values[index] > rhs

print(compare_list_item([0, 1, 2], 1, 0))
print(compare_list_item([False], 0, 0))
print(compare_list_item([0, 1], -1, 0))
print(compare_list_item([0, 1], True, 0))
print(compare_list_item([1], 0, False))

def compare_in_branch(values, index):
    if values[index] > 0:
        return "positive"
    return "not positive"

print(compare_in_branch([1, 0], 0))
print(compare_in_branch([0, 1], 0))

def compare_and_branch(values, index, enabled):
    if values[index] > 0 and enabled:
        return "positive"
    return "not positive"

print(compare_and_branch([1], 0, True))
print(compare_and_branch([0], 0, True))
print(compare_and_branch([True], 0, True))

def advance_permutation(permutation, counts, index, limit):
    insert = permutation.insert
    pop = permutation.pop
    while index != limit:
        insert(index, pop(0))
        counts[index] -= 1
        if counts[index] > 0:
            break
        index += 1
    return permutation, counts, index

print(advance_permutation([0, 1, 2], [1, 2, 3], 0, 3))
print(advance_permutation([0, 1], [True, 2], 0, 2))
print(advance_permutation([0, 1], [1, 2], 2, 2))

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
