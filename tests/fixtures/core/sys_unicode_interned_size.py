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


identifier = "".join(["xlang3", "_unicode_interned_size_", "identifier"])
spaced = "".join(["xlang3", " unicode interned size"])

total_before = sys.getunicodeinternedsize()
immortal_before = sys.getunicodeinternedsize(_only_immortal=True)
print(
    "sys-unicode-interned-size-baseline",
    isinstance(total_before, int),
    isinstance(immortal_before, int),
    immortal_before <= total_before,
)
print(
    "sys-unicode-interned-size-before",
    sys._is_interned(identifier),
    sys._is_immortal(identifier),
    sys._is_interned(spaced),
    sys._is_immortal(spaced),
)

identifier_interned = sys.intern(identifier)
total_after_identifier = sys.getunicodeinternedsize()
immortal_after_identifier = sys.getunicodeinternedsize(_only_immortal=True)
print(
    "sys-unicode-interned-size-identifier",
    sys._is_interned(identifier_interned),
    sys._is_immortal(identifier_interned),
    total_after_identifier >= total_before + 1,
    immortal_after_identifier == immortal_before,
)

spaced_interned = sys.intern(spaced)
total_after_spaced = sys.getunicodeinternedsize()
immortal_after_spaced = sys.getunicodeinternedsize(_only_immortal=True)
print(
    "sys-unicode-interned-size-spaced",
    sys._is_interned(spaced_interned),
    sys._is_immortal(spaced_interned),
    total_after_spaced >= total_after_identifier + 1,
    immortal_after_spaced == immortal_after_identifier,
)
print(
    "sys-unicode-interned-size-falsey",
    sys.getunicodeinternedsize(_only_immortal=[]) >= total_after_spaced,
)
