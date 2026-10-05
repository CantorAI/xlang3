# Eager async frame-vector reuse trial (2026-10-03)

## Result

The full pyperformance run measured Release XLang3 `async_tree_eager` at
**3.165 s ± 0.176 s** (median 3.188 s), versus **3.418 s ± 0.264 s** in the
previous full run. That is a **1.080× speedup** (7.4% less elapsed time). The
separate focused fast-mode run measured **2.985 s ± 0.073 s** (median 2.966 s),
but pyperf warned its sample count was
not sufficient for a stable result. CPython 3.14.7 measured **86.62 ms ±
1.69 ms**, so the full-run XLang3 result remains **36.5× slower** by mean.

The overall geometric mean moved from **0.14699×** to **0.14957×** CPython/XLang3
across the 51 matched subtests (larger favors XLang3). Three measured wins and
50 failures/timeouts remain, the same totals as the preceding full run. This is
a measurable but modest improvement, not a broad performance reversal.

An instrumented eager-tree body run counted about **897,000 native calls**,
**103,000 generator allocations**, and **254,000 instances**. These counters
help choose the next target but are not benchmark scores; the full list is
[`async-tree-frame-vector-pool-counters-20261003.txt`](data/async-tree-frame-vector-pool-counters-20261003.txt).

| Runtime and run | Mean | Standard deviation | Median |
| --- | ---: | ---: | ---: |
| XLang3 before frame reuse, full fast run | 3.418 s | 0.264 s | 3.325 s |
| XLang3 with frame reuse, full fast run | 3.165 s | 0.176 s | 3.188 s |
| XLang3 with frame reuse, focused fast run | 2.985 s | 0.073 s | 2.966 s |
| CPython 3.14.7 reference, full fast run | 86.62 ms | 1.69 ms | 87.12 ms |

## Change

`Interpreter::run_function` now keeps a small thread-local array of frame
vectors, indexed by active C++ interpreter nesting depth. Eager asyncio task
construction frequently enters the interpreter recursively and completes
synchronously. On that path, each nested activation can reset already-cleared
`VMFrame` slots and reuse their locals, registers, instruction-cache storage,
and vector capacities instead of rebuilding them for each child task.

The pool retains storage only after successful completion with no live frames.
Suspended, paused, exceptional, and deeper-than-32 activations keep the existing
frame ownership behavior. Before reuse, the pool drops module, closure,
execution-metadata, and prepared-function references so idle worker threads do
not keep Python objects or imported modules alive. The implementation and its
ownership rationale are commented beside the pool in
`src/executor/xlang_vm/xlang_vm_loop.cpp`.

## Validation and provenance

- Four Python 3.14 fixture checks passed: `asyncio_runtime_edges`,
  `async_taskgroup_current_task`, `asyncio_task_init_override`, and
  `generator_resume_state_reuse`.
- The C++ `xlang3_runtime_value_tests` and `xlang3_interpreter_tests` passed.
- The broad Python fixture sweep passed other than pre-existing environment and
  compatibility issues: three fixtures require libffi/Windows-console ctypes,
  and `importlib_raw_magic_number` expects a private `importlib.util` attribute
  missing from this runtime.
- The focused pyperformance 1.14.0 run used the repository's compatibility
  shim, shared CPython 3.14 dependencies, and the fixed executable path
  `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`.
- Candidate executable SHA-256:
  `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.
- Candidate runtime DLL SHA-256:
  `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.
- Focused candidate raw data is in
  `doc/performance/data/async-tree-frame-vector-pool-focused-fast-20261003.json`.
- The full 97-definition fast run completed with 47 definitions measured and
  50 failures/timeouts. Its full report, chart, raw JSON, run log, and all-case
  status are recorded in
  `pyperformance-xlang3-frame-vector-pool-full-fast-20261003.md` and the
  corresponding `doc/performance/data/pyperformance-xlang3-frame-vector-pool-full-fast-20261003-*`
  files. The targeted C++ runtime/interpreter tests passed. The remaining
  fixture sweep passed except for existing libffi/Windows-console dependencies
  and the `importlib.util._RAW_MAGIC_NUMBER` compatibility gap. The previous
  full comparison is
  `pyperformance-xlang3-decimal-string-full-fast-20261003.md`.
