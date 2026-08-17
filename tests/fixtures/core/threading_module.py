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
