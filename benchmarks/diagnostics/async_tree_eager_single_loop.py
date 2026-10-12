"""Run one official pyperformance eager async-tree workload on a fresh loop.

This keeps ``EagerAsyncTree.run`` and its standard-library implementation
unchanged while exposing one repeatable body for order-balanced XLang3 A/B
measurements. Scores from this helper are diagnostic, not official pyperf data.
"""
import asyncio
import importlib.util
from pathlib import Path

import pyperformance


benchmark_path = (
    Path(pyperformance.__file__).parent
    / "data-files"
    / "benchmarks"
    / "bm_async_tree"
    / "run_benchmark.py"
)
spec = importlib.util.spec_from_file_location("bm_async_tree", benchmark_path)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load official benchmark source: {benchmark_path}")
benchmark_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark_module)


def main():
    tree = benchmark_module.EagerAsyncTree()
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(tree.run())
    finally:
        asyncio.set_event_loop(None)
        loop.close()


main()
