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
import inspect


def outer():
    return inner()


def inner():
    frame = inspect.currentframe()
    print(frame.f_back.f_code.co_name)
    print(frame.f_code.co_firstlineno)
    print("debug_frame_metadata.py" in frame.f_code.co_filename)


code = compile("x = 1", "virtual_file.py", "exec")
print(code.co_filename)
print(code.co_firstlineno)
outer()
