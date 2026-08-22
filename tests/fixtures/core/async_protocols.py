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

class Counter:
    def __init__(self, limit):
        self.i = 0
        self.limit = limit

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.limit:
            raise StopAsyncIteration
        self.i += 1
        return self.i

async def collect():
    total = 0
    async for item in Counter(3):
        total += item
    else:
        total += 10
    return total

print(asyncio.run(collect()))

async def break_loop():
    total = 0
    async for item in Counter(5):
        total += item
        if item == 2:
            break
    else:
        total += 100
    return total

print(asyncio.run(break_loop()))

class AsyncManager:
    async def __aenter__(self):
        return "entered"

    async def __aexit__(self, exc_type, exc, tb):
        print(exc_type is ValueError)
        return True

async def use_manager():
    async with AsyncManager() as value:
        print(value)
        raise ValueError("hidden")
    return "after"

print(asyncio.run(use_manager()))
