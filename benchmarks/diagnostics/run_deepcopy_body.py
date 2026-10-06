"""Run the unchanged pyperformance deepcopy body without pyperf workers.

This is a workload driver for external profilers, not a benchmark score. The
official benchmark module and its deepcopy logic are loaded directly; the
only shim supplies pyperf.perf_counter. The loop count is controlled through
DEEPCOPY_DIAG_LOOPS so CPython and XLang3 can run the same body length.
"""
from __future__ import annotations

import importlib.util
import os
import runpy
import sys
import time
import types


if len(sys.argv) != 2:
    raise SystemExit("usage: run_deepcopy_body.py PATH_TO_BM_DEEPCOPY_RUN_BENCHMARK")

pyperf = types.ModuleType("pyperf")
pyperf.perf_counter = time.perf_counter
sys.modules["pyperf"] = pyperf
namespace = runpy.run_path(sys.argv[1])
loops = int(os.environ.get("DEEPCOPY_DIAG_LOOPS", "100"))
body = namespace["benchmark"]
start = time.perf_counter()
reported = body(loops)
elapsed = time.perf_counter() - start
print(f"deepcopy body: loops={loops}; body={elapsed:.6f}s; reported={reported:.6f}s")
