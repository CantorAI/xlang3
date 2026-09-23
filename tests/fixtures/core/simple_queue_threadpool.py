# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

from concurrent.futures import ThreadPoolExecutor
from _queue import Empty, SimpleQueue


queue = SimpleQueue()
queue.put("first", block=False, timeout=0)
queue.put_nowait("second")
print(queue.get(block=True), queue.get_nowait(), queue.empty())

try:
    queue.get(block=False)
except Empty:
    print("empty")

with ThreadPoolExecutor(max_workers=1) as executor:
    print(executor.submit(lambda: 42).result())

blocking_queue = SimpleQueue()
with ThreadPoolExecutor(max_workers=1) as executor:
    pending = executor.submit(blocking_queue.get, block=True)
    blocking_queue.put(99)
    print(pending.result())
