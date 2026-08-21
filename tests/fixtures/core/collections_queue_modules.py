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
d.clear()
print(d.__len__())
