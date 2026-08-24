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

# Generic truth and callable helpers.
print(all([1, True, "x"]), all([1, 0, 2]), any([0, "", "y"]))
print(callable(lambda x: x), callable(42))

# Numeric conversion/display helpers.
print(bin(10), oct(10), hex(255), bin(-3))
print(pow(2, 5), pow(2, 5, 7), divmod(17, 5))

# Character and ordinal helpers.
print(ord("A"), chr(65), chr(0x2603) == "\u2603")
try:
    chr(0x110000)
except ValueError:
    print("chr-range")

# Hashing uses the shared hashability/equality policy.
print(hash(1) == hash(True), hash((1, "a")) == hash((1, "a")))
try:
    hash([])
except TypeError:
    print("hash-unhashable")

# repr/format remain available as builtin functions.
print(repr("x"), format(255, "#x"), format("hi", ">4"))
