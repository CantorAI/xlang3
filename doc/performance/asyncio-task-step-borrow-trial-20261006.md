# Borrow Task state during each asyncio step (2026-10-06)

## Result

Rejected as a performance change. The candidate borrows the Task-owned
coroutine and loop Values rather than copying them on every task transition.
The scaled diagnostic proves that it removes reference-count operations, but
the official `async_tree_none` pyperformance comparison found no significant
timing difference. The candidate source change has been removed.

| XLang3 Release build | `async_tree_none` mean ± standard deviation |
|---|---:|
| Control | 4.41 s ± 0.04 s |
| Candidate | 4.42 s ± 0.02 s |

`pyperf compare_to` reported the result as not significant. A rigorous control
run exceeded its 600-second full-case cap before producing a score, so there is
no rigorous result for this trial.

## Mechanistic evidence

With perf counters enabled, 100 scaled trees (`levels=4`, `branches=3`) reduced
Generator retain/release operations from 228,424/244,834 to
212,124/228,534, and Instance retain/release operations from
2,457,372/2,559,821 to 2,441,072/2,543,521. That is 16,300 fewer retains and
releases for each of those two object kinds. Instrumented elapsed times were
1.360 s and 1.283 s, respectively; because counters affect execution, those
times are diagnostic and are not a speed claim.

The unchanged official score shows these two removed copies are not a material
share of the measured workload. Do not repeat this borrow-only optimization as
a standalone async-tree fix. The remaining async-tree gap requires a larger
reduction in VM dispatch, frame resume, or scheduler transition work.

## Reproduction and artifacts

- pyperformance 1.14.0 fast control and candidate JSON/logs are in
  [control JSON](data/asyncio-task-step-borrow-control-fast-20261006.json),
  [candidate JSON](data/asyncio-task-step-borrow-candidate-fast-20261006.json),
  [control log](data/asyncio-task-step-borrow-control-fast-20261006.log), and
  [candidate log](data/asyncio-task-step-borrow-candidate-fast-20261006.log).
- Perf-counter outputs are in
  [`control`](data/asyncio-task-step-borrow-control-counters-20261006.txt) and
  [`candidate`](data/asyncio-task-step-borrow-candidate-counters-20261006.txt).
- The rigorous timeout log is
  [`here`](data/asyncio-task-step-borrow-control-rigorous-timeout-20261006.log).
- CPython reference remains 3.14.7 at `C:\Python\Python314`; the pyperformance
  benchmark was `async_tree` (`async_tree_none`).
- Control runtime DLL SHA-256:
  `44BD33CEF4B9CFD42D766130ED81BF9AE68A85CF101889B2657FB5306E47B82E`.
- Candidate runtime DLL SHA-256:
  `B9833BF675635719646150AA82A2D8780D51839111A9EF6B1A897F2C98E077F5`.

The full Python fixture suite passed on the restored Release build after the
candidate was removed. Release CTest passed 54/55 tests; the existing
`xlang3_cli_visual_studio_debugpy_launch` profile assertion failed because its
Visual Studio profile does not use `xlang3.exe` directly, unrelated to this
trial or its removed source change.
