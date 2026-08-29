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


def catch(call):
    try:
        call()
    except Exception as exc:
        return type(exc).__name__, str(exc)
    return "no-error", ""


print("cpu-count-config-value", sys._get_cpu_count_config(), type(sys._get_cpu_count_config()).__name__)
print("cpu-count-config-doc", sys._get_cpu_count_config.__doc__)
print("cpu-count-config-signature", sys._get_cpu_count_config.__text_signature__)
print("cpu-count-config-positional", catch(lambda: sys._get_cpu_count_config(1)))
print("cpu-count-config-keyword", catch(lambda: sys._get_cpu_count_config(x=1)))
