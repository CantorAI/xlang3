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
import asyncio

async def add(a, b):
    return a + b

async def main():
    first = await add(20, 22)
    second = await add(1, 2)
    return first + second

print(asyncio.run(main()))

created = asyncio.create_task(add(5, 6))
print(asyncio.run(created))

async def from_task():
    return await asyncio.create_task(add(7, 8))

print(asyncio.run(from_task()))
print(asyncio.gather([add(2, 3), asyncio.create_task(add(4, 5))]))
