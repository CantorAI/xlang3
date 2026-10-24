"""Warm the official pyperformance deepcopy body and dump CPython 3.14 bytecode."""
from __future__ import annotations

import dis
import runpy
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: dump_deepcopy_cpython314_dis.py PATH_TO_BM_DEEPCOPY_RUN_BENCHMARK")

namespace = runpy.run_path(sys.argv[1])
namespace["benchmark"](int(__import__("os").environ.get("DEEPCOPY_DIAG_LOOPS", "100")))
targets = (
    __import__("copy").deepcopy,
    __import__("copy")._deepcopy_list,
    __import__("copy")._deepcopy_dict,
    __import__("copy")._reconstruct,
)
for function in targets:
    print(f"\n### {function.__module__}.{function.__name__}")
    dis.dis(function, adaptive=True, show_caches=True)
