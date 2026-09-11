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

import _collections
import _queue

q = _queue.SimpleQueue()
print(q.empty())
q.put("a")
q.put_nowait("b")
print(q.qsize())
print(q.get())
print(q.get_nowait())
print(q.empty())

d = _collections.deque([2, 3])
d.append(4)
d.appendleft(1)
d.extend([5, 6])
d.extendleft([0, -1])
print(d.to_list())
print(d.count(3))
print(d.popleft())
print(d.pop())
print(d.to_list())
print(repr(d))
d.rotate(2)
print(d.to_list(), d.index(3))
indexed = _collections.deque("ABCABC")
print(indexed.index("B", 2), indexed.index("A", -3, -1))

class DequeIndex:
    def __index__(self):
        return 1

index_value = DequeIndex()
indexed[index_value] = "Z"
del indexed[index_value]
index_driven = _collections.deque([1, 2, 3], maxlen=index_value)
rotation = _collections.deque([1, 2, 3])
rotation.rotate(index_value)
rotation.insert(index_value, 9)
print(indexed[0], index_driven.to_list(), rotation.to_list(),
      (_collections.deque([7]) * index_value).to_list(),
      _collections.deque("ABCABC").index("C", index_value))

class BadDequeIndex:
    def __index__(self):
        return "not-an-int"

for operation in (
    lambda: _collections.deque().rotate(BadDequeIndex()),
    lambda: _collections.deque([1], maxlen=1).insert(BadDequeIndex(), 2),
):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)

class Matcher:
    def __eq__(self, value):
        return value == "match"

class Mutator:
    def __init__(self, target):
        self.target = target
    def __eq__(self, value):
        self.target.clear()
        return False

comparison_deque = _collections.deque([Matcher(), "other"])
print(comparison_deque.count("match"), "match" in comparison_deque, comparison_deque.index("match"))
mutating_deque = _collections.deque()
mutating_deque.append(Mutator(mutating_deque))
try:
    mutating_deque.count("x")
except RuntimeError:
    print("mutation detected")
d.reverse()
d.insert(1, 99)
print(d.to_list())
bounded = _collections.deque([1, 2], maxlen=2)
print(repr(bounded))
copied = d.copy()
copied.append(42)
print(copied.to_list(), d.to_list())
print(list(reversed(d)))
print(_collections.deque([1, 2]) == _collections.deque([1, 2]), _collections.deque([1]) < _collections.deque([2]))
print((_collections.deque([1, 2]) + _collections.deque([3])).to_list())
print((_collections.deque([1, 2], maxlen=3) * 2).to_list())
import copy
import pickle
pickled = _collections.deque([1, 2], maxlen=3)
print(copy.copy(pickled).to_list(), copy.copy(pickled).maxlen)
print(pickle.loads(pickle.dumps(pickled, 4)).to_list())
reduced = pickled.__reduce__()
print(len(reduced), reduced[1], reduced[2] is None, list(reduced[3]))
print(pickled.__reduce_ex__(index_value)[0] is _collections.deque)
defaults = _collections.defaultdict(list)
defaults["x"].append(1)
print(copy.copy(defaults)["x"], pickle.loads(pickle.dumps(defaults, 4))["x"])
merged = defaults | {"y": 2}
defaults |= {"z": 3}
print(type(merged).__name__, merged.default_factory is list, merged["y"], defaults["z"])
reflected = {"left": 1} | _collections.defaultdict(list, {"right": 2})
print(type(reflected).__name__, reflected.default_factory is list, reflected["left"], reflected["right"])
missing_defaults = _collections.defaultdict(None)
try:
    missing_defaults["missing"]
except KeyError as exc:
    print(exc.args == ("missing",))
d.clear()
print(d.__len__())
size_small = _collections.deque()
size_large = _collections.deque([1, 2, 3])
print(size_small.__sizeof__() > 0, size_large.__sizeof__() > size_small.__sizeof__())

iterated = _collections.deque([1, 2])
forward = iter(iterated)
print(next(forward))
iterated.append(3)
try:
    next(forward)
except RuntimeError:
    print("iterator mutation detected")
backward_values = _collections.deque([1, 2])
backward = reversed(backward_values)
print(next(backward))
backward_values.append(3)
try:
    next(backward)
except RuntimeError:
    print("reverse iterator mutation detected")
