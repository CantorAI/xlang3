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
import sys
import threading


print(sys.gettrace())
print(threading.gettrace())


def tracer(frame, event, arg):
    return tracer


sys.settrace(tracer)
threading.settrace(tracer)

print(sys.gettrace().__name__)
print(threading.gettrace().__name__)

sys.settrace(None)
threading.settrace(None)

print(sys.gettrace())
print(threading.gettrace())
