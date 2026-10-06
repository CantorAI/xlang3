"""Count asyncio Python and native-call events in official async_tree_none.

Run once under CPython 3.14.7 and once under XLang3. Profiling changes timing;
these counts are diagnostic and are not benchmark scores.
"""

import asyncio
import collections
import os
import runpy
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: async_tree_official_call_profile.py PATH_TO_RUN_BENCHMARK_PY")

python_calls = collections.Counter()
native_calls = collections.Counter()


def profile(frame, event, arg):
    if event == "call":
        code = frame.f_code
        filename = code.co_filename.replace("\\", "/")
        if "/asyncio/" in filename:
            name = getattr(code, "co_qualname", code.co_name)
            python_calls[os.path.basename(filename) + ":" + name] += 1
    elif event == "c_call" and getattr(arg, "__module__", None) == "_asyncio":
        name = getattr(arg, "__qualname__", getattr(arg, "__name__", "unknown"))
        native_calls[name] += 1


print("executable", sys.executable)
print("Task", asyncio.Task.__module__, asyncio.Task.__qualname__)
print("Future", asyncio.Future.__module__, asyncio.Future.__qualname__)
sys.setprofile(profile)
try:
    namespace = runpy.run_path(sys.argv[1], run_name="xlang3_async_tree_profile")
    asyncio.run(namespace["NoneAsyncTree"]().run())
finally:
    sys.setprofile(None)

print("Python asyncio call events", sum(python_calls.values()))
for name, count in python_calls.most_common(60):
    print(count, name)
print("visible native _asyncio call events", sum(native_calls.values()))
for name, count in native_calls.most_common(30):
    print(count, name)
