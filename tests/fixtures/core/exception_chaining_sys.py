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

import sys

print(sys.exc_info())

try:
    try:
        raise ValueError("inner")
    except ValueError as inner:
        info = sys.exc_info()
        print(info[0], info[1])
        raise RuntimeError("outer") from inner
except RuntimeError as err:
    info = sys.exc_info()
    print(info[0], info[1])
    print(err.__cause__)
    print(err.__context__)
    print(err.__suppress_context__)
    print(info[2] is None)

try:
    raise TypeError("again")
except TypeError:
    try:
        raise
    except TypeError as err:
        print("reraised", err)

print(sys.exc_info())
