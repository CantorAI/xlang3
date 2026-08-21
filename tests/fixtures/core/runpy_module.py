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

import runpy

module_ns = runpy.run_module("runpy_support")
print(module_ns["result"])

path = "xlang3_runpy_path.py"
with open(path, "w") as f:
    f.write("x = 4\ny = x + 5\n")

path_ns = runpy.run_path(path, "custom_name")
print(path_ns["__name__"])
print(path_ns["y"])
