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

def fail_in_function():
    print("before fail")
    print([1][3])
    print("not reached")

try:
    fail_in_function()
except:
    print("caught from function")

class SuppressManager:
    def __enter__(self):
        print("enter suppress")
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        print(exc_type)
        print(exc_value)
        print(traceback)
        print("exit suppress")
        return True

class PropagateManager:
    def __enter__(self):
        print("enter propagate")
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        print(exc_type)
        print(exc_value)
        print(traceback)
        print("exit propagate")
        return False

with SuppressManager():
    print([1][4])
print("after suppress")

try:
    with PropagateManager():
        fail_in_function()
except:
    print("caught propagated")
