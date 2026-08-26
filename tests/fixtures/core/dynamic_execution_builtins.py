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

x = 4
print(eval("x + 6"))

expr = compile("x * 3", "<test>", "eval")
print(eval(expr))

exec("y = x + 5")
print(y)

stmt = compile("z = y + 1", "<test>", "exec")
print(exec(stmt))
print(z)

def capture(a):
    b = a + 1
    values = locals()
    return values["a"] + values["b"]

print(capture(7))

# globals() is a live module mapping: writes update later global lookup.
globals()["g_live"] = 41
def read_live():
    return g_live + 1
print(read_live(), globals().get("g_live"), "g_live" in globals())
print("__name__" in globals().keys(), globals().setdefault("g_default", 5))
globals().update({"g_live": 50})
print(read_live(), globals().pop("g_default"), "g_default" in globals())
print(any(name == "g_live" and value == 50 for name, value in globals().items()))
del globals()["g_live"]
print("g_live" in globals())

try:
    compile("if", "<bad>", "eval")
except SyntaxError:
    print("syntax")
