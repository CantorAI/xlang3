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
import math

THREADS = 4
COUNT = 180000

threads = []
partials = []
lock = threading.Lock()

def worker(worker_id):
    base = worker_id * COUNT
    total = 0
    i = 0
    while i < COUNT:
        value = base + i
        angle = value * 0.00001
        wave = math.sin(angle) * math.cos(angle * 0.5) + math.sin(angle * 0.25)
        if wave > 0.25:
            total = total + value * 3 - value % 7
        else:
            total = total - value * 2 + value % 11
        i = i + 1

    lock.acquire()
    partials.append(total)
    lock.release()

i = 0
while i < THREADS:
    thread = threading.Thread(None, worker, None, (i,))
    threads.append(thread)
    thread.start()
    i = i + 1

i = 0
while i < len(threads):
    threads[i].join()
    i = i + 1

total = 0
i = 0
while i < len(partials):
    total = total + partials[i]
    i = i + 1

print(total)
