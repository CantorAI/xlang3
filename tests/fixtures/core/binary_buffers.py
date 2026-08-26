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

raw = b"\x00\x7f\x80\xff"
print(raw, len(raw), raw[2], raw[3])

ba = bytearray(b"abc")
ba[1] = 90
ba.append(33)
ba.extend(b"?")
print(ba, len(ba), bytes(ba))

mv = memoryview(ba)
print(len(mv), mv[0], mv[1:3].tobytes())
mv[0] = 65
print(ba, mv.tobytes())
print(mv.readonly, mv.format, mv.itemsize, mv.ndim, mv.shape, mv.strides, mv.c_contiguous, mv.contiguous)
print(mv.tolist(), mv.hex(), mv == bytes(ba), mv[1:4] == b"Zc!")
mv[1:3] = b"xy"
print(ba, mv.cast("B").format, mv.cast("b", (5,)).tolist())
print(mv.count(120), mv.index(120), mv.index(b"y"), mv.index(33, 3), mv[(2,)])
try:
    mv.index(111)
except ValueError:
    print("memoryview-index-miss")
readonly = mv.toreadonly()
print(readonly.readonly, readonly.obj is ba, readonly.tolist())
mv[0] = 66
print(readonly.tolist()[0], readonly.hex("-"), readonly.hex("-", 2), readonly.hex("-", -2))
print(readonly.tobytes("A"), readonly.cast("B", [5]).tolist())

ro = memoryview(b"xy")
print(ro[1], ro[0:1].tobytes(), bytes(ro))
with memoryview(b"ok") as ctx:
    print(ctx.tobytes(), ctx.readonly)
try:
    ctx.tobytes()
except RuntimeError:
    print("released")
print(type(b) is bytes, type(ba) is bytearray, type(mv) is memoryview)
print(bytes(mv), bytearray(mv))
