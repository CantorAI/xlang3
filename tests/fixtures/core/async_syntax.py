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
async def add(a, b):
    return a + b

async def main():
    first = await add(20, 22)
    second = await add(1, 2)
    return first + second

ran = []

async def lazy_value():
    ran.append("ran")
    return 99

main_coro = main()
coro = lazy_value()
print(str(main_coro))
print(len(ran), str(coro))
