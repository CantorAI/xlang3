#
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
#

x = 2 ** 128
print("bigint-pow", x > 0, x.bit_length(), str(x).startswith("340282366920938463463"))

y = 2 ** 70
print("bigint-arith", y + 5 > y, y - 1 < y, y * y == 2 ** 140)

mask = 2 ** 128 - 1
print("bigint-bit", (mask >> 64) == (2 ** 64 - 1), ((2 ** 65) & (2 ** 65 - 1)) == 0)

raw = int.from_bytes(bytes([255]) * 16, "big")
print("bigint-bytes", raw.bit_length(), raw == mask)

print("bigint-negative", (-5) >> 1, (-2) << 70 < 0, ~mask == -(2 ** 128))
