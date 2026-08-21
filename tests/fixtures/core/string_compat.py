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

print("A\nB")
print("\x41\u0042\U00000043")
print(b"A\nB")
print(b"\x41BC"[0], b"BC" in b"ABC", 66 in b"ABC", b"ABC"[1:])

name = "XLang3"
count = 7
print(f"{name}:{count + 1}")
print(f"{{{name}}}")

print("{} {}".format("hello", "world"))
print("{1}-{0}".format("zero", "one"))
print("|".join("a,b,c".split(",")))
print("-".join(["a", "b", "c"]))
print("banana".replace("na", "NA", 1))
print("banana".find("na"), "banana".count("na"))
print("abc".startswith("a"), "abc".endswith("c"))
print("Hi".encode().decode())

parts = "  alpha,beta,gamma  ".strip().replace("a", "A").split(",")
print(parts)
print(len(parts[0]), len(parts[1]), parts[2].startswith("gA"))
