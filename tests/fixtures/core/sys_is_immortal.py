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


class UserClass:
    pass


dynamic_string = "".join(["a", "b", "c"])
dynamic_spaced = "".join(["abc", " def"])
interned_spaced = sys.intern(dynamic_spaced)

print(
    "sys-is-immortal-singletons",
    sys._is_immortal(None),
    sys._is_immortal(True),
    sys._is_immortal(False),
    sys._is_immortal(Ellipsis),
    sys._is_immortal(NotImplemented),
)
print(
    "sys-is-immortal-small-int-bounds",
    sys._is_immortal(-6),
    sys._is_immortal(-5),
    sys._is_immortal(0),
    sys._is_immortal(256),
    sys._is_immortal(257),
    sys._is_immortal(1000),
)
print(
    "sys-is-immortal-strings",
    sys._is_immortal(""),
    sys._is_immortal("abc"),
    sys._is_immortal(dynamic_string),
    sys._is_immortal(interned_spaced),
)
print(
    "sys-is-immortal-objects",
    sys._is_immortal(()),
    sys._is_immortal((1,)),
    sys._is_immortal(object),
    sys._is_immortal(int),
    sys._is_immortal(UserClass),
    sys._is_immortal([]),
)
