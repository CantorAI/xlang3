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

from enum import IntEnum, _simple_enum
from enum import ReprEnum

print("intenum-use-args", IntEnum._use_args_)

class MyIntEnum(int, ReprEnum):
    pass

print("myintenum", MyIntEnum._member_type_, MyIntEnum._use_args_)

@_simple_enum(IntEnum)
class MiniStatus:
    def __new__(cls, value, phrase, description=""):
        obj = int.__new__(cls, value)
        obj._value_ = value
        obj.phrase = phrase
        obj.description = description
        return obj

    OK = 200, "OK", "done"

print(MiniStatus.OK.value, MiniStatus.OK.phrase, MiniStatus.OK.description)
