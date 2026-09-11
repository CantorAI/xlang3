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
defaults = _collections.defaultdict(list)
defaults["x"].append(1)
print(copy.copy(defaults)["x"], pickle.loads(pickle.dumps(defaults, 4))["x"])
d.clear()
print(d.__len__())
