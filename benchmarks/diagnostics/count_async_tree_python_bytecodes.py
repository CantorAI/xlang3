"""Count CPython opcodes for the official async_tree_none body.

The trace callback perturbs runtime, so this script reports instruction counts
only and must never be used as a timing benchmark.
"""

import asyncio
import collections
import runpy
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: count_async_tree_python_bytecodes.py PATH_TO_RUN_BENCHMARK_PY")

opcodes = collections.Counter()
calls = collections.Counter()


def trace(frame, event, arg):
    if event == "call":
        code = frame.f_code
        calls[code.co_qualname] += 1
        frame.f_trace_opcodes = True
    elif event == "opcode":
        opcodes[frame.f_code.co_qualname] += 1
    return trace


sys.settrace(trace)
try:
    namespace = runpy.run_path(sys.argv[1], run_name="xlang3_async_tree_counted")
    asyncio.run(namespace["NoneAsyncTree"]().run())
finally:
    sys.settrace(None)

print("python_function_calls", sum(calls.values()))
print("python_opcode_dispatches", sum(opcodes.values()))
for name, count in opcodes.most_common(30):
    print(count, name)
