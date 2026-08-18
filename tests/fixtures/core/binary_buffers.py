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

b = bytes([65, 0, 66])
print(b, len(b), b[0], b[1], b[1:])

ba = bytearray(b"abc")
ba[1] = 90
ba.append(33)
ba.extend(b"?")
print(ba, len(ba), bytes(ba))

mv = memoryview(ba)
print(len(mv), mv[0], mv[1:3].tobytes())
mv[0] = 65
print(ba, mv.tobytes())

ro = memoryview(b"xy")
print(ro[1], ro[0:1].tobytes(), bytes(ro))
print(type(b) is bytes, type(ba) is bytearray, type(mv) is memoryview)
print(bytes(mv), bytearray(mv))
