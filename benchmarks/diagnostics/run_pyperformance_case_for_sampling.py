"""Repeat one official pyperformance function for native IP sampling.

This is a profiling workload, not a timing harness. Use pyperformance itself
for any score or before/after performance claim.
"""

import argparse
import runpy
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("benchmark_script", type=Path)
    parser.add_argument("case")
    parser.add_argument("--repeat", type=int, default=100)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")

    namespace = runpy.run_path(str(args.benchmark_script), run_name="sampling_target")
    benchmark = namespace["BENCHMARKS"][args.case]
    benchmark(args.repeat)
    print(f"completed {args.case} profiling workload x{args.repeat}")


if __name__ == "__main__":
    main()
