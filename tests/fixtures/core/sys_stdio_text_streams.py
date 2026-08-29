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

read_empty = sys.stdin.read(0)
readline_empty = sys.stdin.readline(0)
print(type(read_empty).__name__, repr(read_empty))
print(type(readline_empty).__name__, repr(readline_empty))
print(type(sys.stdin).__module__, type(sys.stdin).__qualname__, sys.stdin.mode, sys.stdin.name)
