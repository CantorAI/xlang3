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

try:
    raise RuntimeError("typed boom")
except TypeError:
    print("wrong")
except RuntimeError as err:
    print("runtime caught")
    print(err)

try:
    print(len())
except TypeError as err:
    print("type caught")
    print(err)

try:
    raise ValueError("value boom")
except TypeError:
    print("wrong")
except:
    print("bare fallback")

try:
    raise ImportError("import boom")
except ImportError as err:
    print("import caught")
    print(err)
