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

class NewOwner:
    def __new__(cls, value, phrase, description=""):
        obj = object.__new__(cls)
        obj.value = value
        obj.phrase = phrase
        obj.description = description
        return obj

raw_new = NewOwner.__dict__["__new__"]
print(type(raw_new).__name__)
new_func = raw_new.__func__
item = new_func(NewOwner, *(200, "OK", "done"))
print(item.value, item.phrase, item.description)

class IntNewOwner(int):
    def __new__(cls, value, phrase, description=""):
        obj = int.__new__(cls, value)
        obj.phrase = phrase
        obj.description = description
        return obj

int_raw_new = IntNewOwner.__dict__["__new__"].__func__
int_item = int_raw_new(IntNewOwner, *(201, "Created", "made"))
print(int_item, int_item.phrase, int_item.description, isinstance(int_item, IntNewOwner))
