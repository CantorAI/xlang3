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

items = [2, 3]
more = (5, 6)

print([1, *items, 4])
print((*items, 4, *more))
expanded = [*range(3), *"ab"]
print(expanded[0], expanded[1], expanded[2], "".join(expanded[3:]))
print([*b"AZ"])

s = {1, *items, 4, *more}
print(len(s), 1 in s, 6 in s, 9 in s)
