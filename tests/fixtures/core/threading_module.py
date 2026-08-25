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
import _thread
import sys

values = []
lock = threading.Lock()
done = _thread.allocate_lock()
done.acquire()

def worker(value):
    lock.acquire()
    values.append(value)
    lock.release()
    done.release()

t = threading.Thread(None, worker, None, (11,))
t.start()
done.acquire()
done.release()
t.join()

print(len(values))
print(values[0])

low_done = _thread.allocate_lock()
low_done.acquire()

def low_worker(value):
    print(value)
    low_done.release()

_thread.start_new_thread(low_worker, (22,))
low_done.acquire()
low_done.release()
print("thread-ok")

trace_done = _thread.allocate_lock()
trace_done.acquire()
trace_seen = []

def trace_func(frame, event, arg):
    return trace_func

def trace_worker():
    trace_seen.append(sys.gettrace().__name__)
    trace_done.release()

threading.settrace(trace_func)
traced = threading.Thread(target=trace_worker)
traced.start()
trace_done.acquire()
trace_done.release()
traced.join()
threading.settrace(None)
print(trace_seen[0])

life_done = _thread.allocate_lock()
life_done.acquire()

def lifecycle_worker():
    life_done.acquire()
    life_done.release()

life = threading.Thread(target=lifecycle_worker, name="worker-one", daemon=True)
print(life.name, life.daemon, life.ident)
life.start()
print(life.ident is None, life.native_id is None, life.is_alive(), threading.active_count() >= 2)
life.join(0)
print(life.is_alive())
life_done.release()
life.join()
print(life.is_alive(), life._is_stopped)
print(threading.main_thread().name, len(threading.enumerate()) >= 1)
