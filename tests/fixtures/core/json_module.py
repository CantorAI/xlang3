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
import json

data = json.loads('{"name":"xlang3","items":[1,2,3],"enabled":true}')
print(data["name"])
print(data["items"][1])
print(data["enabled"])
print(json.loads('{"u":"\\ud83e\\uddea","a":[-4,1e-3,true,null]}'))
print(json.loads('{"same":1,"same":2}'))
print(json.loads('{"raw":"ąć","empty":""}'))
owned_input = ''.join(['{"owned":"data"}'])
owned_result = json.loads(owned_input)
owned_input = ''
print(owned_result["owned"])
print(json.loads('123456789012345678901234567890'))
print(json.loads('[1e400, -1e400, 1e-400, -1e-400, 1.7976931348623157e308]'))
print(json.loads('{"x":4}', parse_int=lambda value: "i:" + value))
try:
    json.loads('[1,]')
except json.JSONDecodeError:
    print("invalid JSON fallback")
print(json.dumps({"a": 1, "b": [2, 3]}))
shared = {"value": 7}
print(json.dumps([shared, shared]))
print(json.dumps({"unicode": "ąćż🧪", "quote": "a\"b\\c", "controls": "\n\t", "nested": [None, True, False, -4, (5, 6)]}))
print(json.dumps({"b": 2, "a": 1}, sort_keys=True))
print(json.dumps({"unicode": "ąćż"}, ensure_ascii=False))
print(json.dumps({1: "integer key"}))
print(json.dumps([1.0, -0.0, 1e20, 1e-7, 2 ** 100]))
cycle = []
cycle.append(cycle)
try:
    json.dumps(cycle)
except ValueError:
    print("cycle detected")
