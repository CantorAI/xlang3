"""Run one official pyperformance asyncio tree workload without pyperf workers.

This preserves the benchmark's AsyncTree implementation and workload shape,
but runs it once in-process so XLang3 VM counters can describe the full tree.
The elapsed time is diagnostic only and must not be compared with pyperf.
"""

import argparse
import asyncio
import runpy
from pathlib import Path

import pyperformance


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("benchmark", choices=("none", "eager", "io", "memoization", "cpu_io_mixed"), default="none")
    parser.add_argument("--task-groups", action="store_true")
    args = parser.parse_args()

    benchmark_path = (
        Path(pyperformance.__file__).parent
        / "data-files"
        / "benchmarks"
        / "bm_async_tree"
        / "run_benchmark.py"
    )
    namespace = runpy.run_path(str(benchmark_path), run_name="async_tree_diagnostic")
    tree_type = namespace["BENCHMARKS"][args.benchmark]
    tree = tree_type(use_task_groups=args.task_groups)
    asyncio.run(tree.run())
    print(f"completed official async_tree_{args.benchmark} task_groups={args.task_groups}")


if __name__ == "__main__":
    main()
