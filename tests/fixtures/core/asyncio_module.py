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

def add(a, b):
    return a + b

def square(x):
    return x * x

t = asyncio.create_task(add, (10, 32))
print(asyncio.run(t))

items = [asyncio.create_task(square, (2,)), asyncio.create_task(square, (5,))]
print(asyncio.gather(items))

print(asyncio.run(add, (1, 2)))

loop = asyncio.new_event_loop()
asyncio.set_event_loop(loop)
print(asyncio.get_event_loop() is loop, asyncio.get_running_loop() is loop)
loop_task = loop.create_task(square(6))
print(loop.run_until_complete(loop_task), loop.is_closed())
print(asyncio.run(asyncio.sleep(0, "slept")))
loop.close()
print(loop.is_closed())
