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

def main():
    total = 0
    outer = 0
    while outer < 1800:
        values = [x * 3 for x in range(80) if x % 3 != 0]
        table = {x: x + outer for x in values if x % 5 != 0}
        unique = {x % 17 for x in values}
        total = total + len(values) + len(table) + len(unique)
        outer = outer + 1
    print(total)


main()
