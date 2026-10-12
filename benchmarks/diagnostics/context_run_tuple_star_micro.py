"""Time exact-tuple ``Context.run(callback, *args)`` calls in this runtime.

This isolates the expanded native-call adapter used by asyncio Handle._run;
results are diagnostic and are not pyperformance scores.
"""

import contextvars
import sys
import time


iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 100_000
context = contextvars.copy_context()
arguments = (1, 2, 3)


def callback(a, b, c):
    return a + b + c


result = 0
start = time.perf_counter()
for _ in range(iterations):
    result = context.run(callback, *arguments)
elapsed = time.perf_counter() - start
if result != 6:
    raise RuntimeError(f"unexpected callback result: {result}")
print(f"context_run_tuple_star_seconds={elapsed:.9f} iterations={iterations}")
