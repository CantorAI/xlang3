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

import urllib.parse

# Integer format specs are used by urllib.parse's byte quoting path.
print("format", "{:02X}".format(32), "{:04x}".format(255), "{:08b}".format(5))

# Keep quote and the internal quoter path covered because both stress dict
# subclass __missing__, map iteration, str.join, and str.format.
print("quote", urllib.parse.quote("a b"), urllib.parse.quote_from_bytes(b"a b"))
q = urllib.parse._byte_quoter_factory(b"/")
print("quoter", q(32), "".join(map(q, b"a b")))
