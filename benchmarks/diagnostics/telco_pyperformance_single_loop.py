"""Run one unchanged pyperformance 1.14.0 ``telco`` loop for matched A/B tests."""
import importlib.util
from pathlib import Path

import pyperformance


benchmark_path = (
    Path(pyperformance.__file__).parent
    / "data-files"
    / "benchmarks"
    / "bm_telco"
    / "run_benchmark.py"
)
spec = importlib.util.spec_from_file_location("bm_telco", benchmark_path)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load official benchmark source: {benchmark_path}")
benchmark_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark_module)
benchmark_data = benchmark_path.parent / "data" / "telco-bench.b"


def main():
    benchmark_module.bench_telco(1, str(benchmark_data))
    print("telco-complete")


main()
