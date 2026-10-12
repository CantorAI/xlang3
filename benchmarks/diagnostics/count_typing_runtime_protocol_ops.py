"""Run the official typing protocol body once for VM opcode attribution.

Diagnostic only: pyperformance owns timing. Import its definition without
starting pyperf workers, then invoke the same bench_protocols loop once.
"""
import runpy
import sys
import time


benchmark_path = sys.argv[1]
loops = int(sys.argv[2]) if len(sys.argv) > 2 else 500
namespace = runpy.run_path(benchmark_path, run_name="typing_protocol_opcount")
start = time.perf_counter()
namespace["bench_protocols"](loops)
print(f"official typing_runtime_protocols body loops={loops} seconds={time.perf_counter() - start:.6f}")
