"""Measure small asyncio task trees while varying tree size.

This isolates Task/Future scheduling overhead from the official pyperformance
tree's very large 6-by-6 workload. Run the same file under CPython and XLang3.
"""

import argparse
import asyncio
import time


async def recurse(levels: int, branches: int) -> None:
    if levels == 0:
        return
    await asyncio.gather(*(recurse(levels - 1, branches) for _ in range(branches)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--levels", type=int, default=3)
    parser.add_argument("--branches", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=50)
    args = parser.parse_args()

    start = time.perf_counter()
    for _ in range(args.iterations):
        asyncio.run(recurse(args.levels, args.branches))
    elapsed = time.perf_counter() - start
    print(
        f"levels={args.levels} branches={args.branches} "
        f"iterations={args.iterations} seconds={elapsed:.9f}"
    )


if __name__ == "__main__":
    main()
