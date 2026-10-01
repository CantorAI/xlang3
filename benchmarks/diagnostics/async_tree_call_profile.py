"""Count asyncio call events on the same small task tree in both runtimes.

This is an instrumented diagnostic, not a benchmark score. CPython's native
Task/Future implementation does not emit Python call events for its internal
steps; the counts identify Python work XLang3 executes around task scheduling.
"""

import argparse
import asyncio
import collections
import os
import sys

from async_tree_scaled import recurse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--levels", type=int, default=3)
    parser.add_argument("--branches", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=5)
    args = parser.parse_args()
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
    print("workload", args.levels, args.branches, args.iterations)
    sys.setprofile(profile)
    try:
        for _ in range(args.iterations):
            asyncio.run(recurse(args.levels, args.branches))
    finally:
        sys.setprofile(None)
    print("Python asyncio call events", sum(python_calls.values()))
    for name, count in python_calls.most_common(40):
        print(count, name)
    print("visible native _asyncio call events", sum(native_calls.values()))
    for name, count in native_calls.most_common(20):
        print(count, name)


if __name__ == "__main__":
    main()
