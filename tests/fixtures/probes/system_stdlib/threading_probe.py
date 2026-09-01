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

import threading

# This must use CPython's pure-Python threading.py over XLang3's native _thread
# dependency primitives.
print("threading-source", threading.__file__.endswith("threading.py"))
print("current", threading.current_thread().name, threading.active_count() >= 1)

values = []
worker = threading.Thread(target=lambda: values.append(7), name="xlang3-worker")
print("before", worker.is_alive(), worker.ident is None)
worker.start()
worker.join()
print("after", values, worker.is_alive(), worker.ident is not None)
print("stack", threading.stack_size())
