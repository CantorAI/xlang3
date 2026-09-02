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

# threading.local must store attributes per native thread, not on the shared
# instance object.
local = threading.local()
local.value = "main"
local_values = []

def set_local_from_worker():
    local_values.append(hasattr(local, "value"))
    local.value = "worker"
    local_values.append(local.value)
    local_values.append(local.__dict__ == {"value": "worker"})

local_worker = threading.Thread(target=set_local_from_worker)
local_worker.start()
local_worker.join()
print("local", local.value, local.__dict__, local_values)

# Lock/RLock/Event/Condition should be driven by CPython's threading.py while
# delegating synchronization to native _thread primitives.
lock = threading.Lock()
print("lock", lock.acquire(False), lock.locked())
lock.release()

rlock = threading.RLock()
print("rlock", rlock.acquire(), rlock.acquire(False), rlock.locked(), rlock._is_owned())
saved = rlock._release_save()
print("rlock-save", saved[0] >= 1, rlock.locked())
rlock._acquire_restore(saved)
print("rlock-restore", rlock.locked(), rlock._is_owned())
rlock.release()
rlock.release()

event = threading.Event()
print("event", event.is_set())
event.set()
print("event-set", event.wait(0), event.is_set())
event.clear()
print("event-clear", event.wait(0), event.is_set())

condition = threading.Condition()
with condition:
    print("condition-owned", condition._lock._is_owned(), condition._is_owned())
    condition.notify_all()

print("stack", threading.stack_size())
