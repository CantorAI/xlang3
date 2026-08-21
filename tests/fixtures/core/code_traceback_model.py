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

def sample(a, b=3):
    local = a + b
    return local

print(sample.__code__.co_name)
print(sample.__code__.co_argcount)
print("a" in sample.__code__.co_varnames)
print("local" in sample.__code__.co_varnames)

def inner():
    raise RuntimeError("boom")

try:
    inner()
except RuntimeError as err:
    tb = err.__traceback__
    last_name = tb.tb_frame.f_code.co_name
    while tb.tb_next != None:
        tb = tb.tb_next
        last_name = tb.tb_frame.f_code.co_name
    print(last_name)
    print(err.__cause__ == None)
    print(err.__context__ == None)
