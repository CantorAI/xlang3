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

def one(x): return x + 1
print(one(1))

class Empty: pass
print(isinstance(Empty(), Empty))

if True: print("if")
if False: print("bad")
else: print("else")
if False: print("bad")
elif True: print("elif")
else: print("bad")

i = 0
while i < 1: print("while"); i += 1

for x in [1, 2]: print("for" + str(x))
for x in []: print("bad")
else: print("for-else")

try: print("try")
except Exception: print("bad")
try: raise ValueError("x")
except ValueError: print("except")
finally: print("finally")

class CM:
    def __enter__(self): return "cm"
    def __exit__(self, exc_type, exc_value, tb): return False

with CM() as value: print(value)

match 2:
    case 1: print("bad")
    case 2: print("case")
    case _: print("bad")

single3_tail = r"[^'\\]*(?:(?:\\.|'(?!''))[^'\\]*)*'''"
double3_tail = r'[^"\\]*(?:(?:\\.|"(?!""))[^"\\]*)*"""'
print(single3_tail.endswith("'''"))
print(double3_tail.endswith('"""'))
