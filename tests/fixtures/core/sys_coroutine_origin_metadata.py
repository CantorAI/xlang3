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

print(sys.get_coroutine_origin_tracking_depth.__doc__ == "Check status of origin tracking for coroutine objects in this thread.")
print("origin tracking for coroutine objects" in sys.set_coroutine_origin_tracking_depth.__doc__)
print("cr_origin attribute" in sys.set_coroutine_origin_tracking_depth.__doc__)
print("Set a depth of 0 to disable." in sys.set_coroutine_origin_tracking_depth.__doc__)
print(sys.get_coroutine_origin_tracking_depth.__text_signature__ == "($module, /)")
print(sys.set_coroutine_origin_tracking_depth.__text_signature__ == "($module, /, depth)")
