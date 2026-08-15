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

def div_zero():
    try:
        x = 1 / 0
        print(x)
    except:
        print("div caught")

def bad_index():
    try:
        print([1][5])
    except:
        print("index caught")

def native_error():
    try:
        print(len())
    except:
        print("native caught")

def missing_name():
    try:
        print(no_such_name)
    except:
        print("name caught")

div_zero()
bad_index()
native_error()
missing_name()
