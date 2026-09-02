#
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
#

import asyncio
import _overlapped


print("asyncio-source", asyncio.__file__.replace("\\", "/").endswith("/Lib/asyncio/__init__.py"))
print("overlapped", _overlapped.NULL, _overlapped.Overlapped().pending)
print("asyncio-import-ok", hasattr(asyncio, "Runner"))


async def add(a, b):
    return a + b


print("asyncio-run", asyncio.run(add(2, 3)))
