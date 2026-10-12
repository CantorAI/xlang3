# Raytrace specialization comparison: CPython 3.14.7 and XLang3 (2026-10-06)

This diagnostic compares the warmed CPython bytecode for the unchanged official
`pyperformance` raytrace benchmark with XLang3's VM-opcode counts. It is evidence
for choosing a shared-runtime optimization; it is not a timing comparison or a
speedup claim.

## CPython 3.14.7

After 20,000 calls to `Vector.dot`, CPython's adaptive disassembly uses:

```text
LOAD_FAST_BORROW other
LOAD_ATTR_METHOD_WITH_VALUES mustBeVector
CALL_PY_EXACT_ARGS 0
POP_TOP
LOAD_FAST_BORROW self
LOAD_ATTR_INSTANCE_VALUE x
LOAD_FAST_BORROW other
LOAD_ATTR_INSTANCE_VALUE x
BINARY_OP_MULTIPLY_FLOAT
... same pattern for y and z ...
BINARY_OP_ADD_FLOAT
BINARY_OP_ADD_FLOAT
RETURN_VALUE
```

This is the output of `dis.dis(..., adaptive=True)` under `C:\Python\Python314\python.exe` (3.14.7), after warming the benchmark's original method. CPython retains the Python method call to `mustBeVector`, while specializing instance-field reads and exact-float arithmetic.

## XLang3

The same official 100x100 benchmark body, run once by the fixed Release XLang3 executable with `--perf-counters`, produced these dispatch counts:

| Operation | Count |
| --- | ---: |
| `LoadLocalAttr` | 5,862,467 |
| `Mul` | 2,471,173 |
| `Add` | 1,246,000 |
| `CallMethod` | 1,047,711 |
| `CallLocalMethod` | 928,757 |

Separate `sys.setprofile` diagnostics counted 2,867,043 Python call events under each runtime. This is not evidence that optimized call/frame counts match: XLang3 explicitly disables Python-function inlining while a profile hook is active (`inline_python_function_allowed`). Treat these as instrumented source-call counts only. XLang3's saved raw VM counters are in [`raytrace-xlang3-vm-counters-20261006.txt`](data/raytrace-xlang3-vm-counters-20261006.txt); call-event counts are in [`raytrace-xlang3-callprofile-20261006.txt`](data/raytrace-xlang3-callprofile-20261006.txt) and [`raytrace-cpython314-callprofile-20261006.txt`](data/raytrace-cpython314-callprofile-20261006.txt).

The CPython disassembly shows a concrete optimization opportunity: XLang3 already has guarded attribute caches and fast numeric helpers, so the next trial should determine whether a fused or cheaper call-site path can reduce their per-operation cost in the generic VM. The counters alone do not establish which operations dominate wall time, and the call-event profiler disables an XLang3 optimization. This finding needs a focused implementation and official `raytrace` plus full fixed-baseline regression measurements before it can be called a gain. The existing guarded attribute path and arithmetic fallback must preserve descriptors, overrides, and non-float values.

## Reproduction identity

- CPython: `C:\Python\Python314\python.exe` 3.14.7.
- Benchmark: pyperformance 1.14 `bm_raytrace/run_benchmark.py`, SHA-256 `88EF4D9060D8E8F6CE40F376477AAF89CC808FA44813225A3071A05A1467F017`.
- Fixed XLang3 executable: `build-repro/main-verify-20261006/Release/xlang3.exe`, SHA-256 `7FB9FD272F16D0374596DA1149F61226F3011BB2EC80539F70900CDBB68B357D`.
- Runtime DLL: SHA-256 `CCC5EC8DDA34F0686799FCBEF0080907B30F27D79F8E44D0C28B9A9EE4C33259`.
- The sampled native profile is diagnostic only: [raw samples](data/raytrace-xlang3-native-samples-20261006.json), 448 instruction-pointer samples from the runtime DLL. It has no call stacks and is not a timing result.
