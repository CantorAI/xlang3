"""Run the official pyperformance async_tree_none body once without pyperf."""

import asyncio
import runpy
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: run_async_tree_none_body.py PATH_TO_RUN_BENCHMARK_PY")

namespace = runpy.run_path(sys.argv[1], run_name="xlang3_async_tree_profile")
asyncio.run(namespace["NoneAsyncTree"]().run())
