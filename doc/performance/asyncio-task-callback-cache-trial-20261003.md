# Asyncio Task callback cache trial (2026-10-03)

## Result

Rejected. Reusing each Task's bound `_step`, `_eager_step`, and `_wakeup`
callbacks did not show a repeatable `async_tree_eager` improvement. The source
and fixed Release runtime were rebuilt without the cache; no callback-cache
fields or cache-specific GC edges remain.

The hypothesis was that each await/reschedule created new `BoundMethod`
objects. The candidate kept those callbacks on native Task state and published
them as native-GC edges, clearing them when the Task completed. This avoided
repeated bound-method allocation while preserving the existing native callback
dispatch behavior.

## Measurement

Both runs used pyperformance 1.14.0 `--fast`, Python 3.14.7's standard
library, the same dependency site and Windows compatibility shim, and the same
benchmark command. Pyperf reported 10 measured runs with 2 values each and
flagged both distributions as unstable.

| Variant | Mean ± standard deviation | Median ± MAD |
|---|---:|---:|
| Original control | 3.36 s ± 0.26 s | 3.25 s ± 0.11 s |
| Cached Task callbacks | 3.47 s ± 0.21 s | 3.42 s ± 0.07 s |

The candidate mean was 3.3% slower, and the distributions overlap broadly.
This does not establish a regression either, but it provides no basis to keep
extra per-Task state, GC-edge bookkeeping, and completion cleanup in a hot
runtime path. The control/candidate difference is within host noise.

## Correctness checks and raw evidence

These focused XLang3 fixtures passed after the cache was removed:

- `task_async`
- `asyncio_runtime_edges`
- `asyncio_native_call_method_dispatch`

The full fixture runner was not rerun for this rejected experiment; its known
`ctypes_pointer_return` failure due to unavailable libffi remains documented
separately. Raw pyperf JSON is in
[`scratch/performance-trials/task-callback-cache-20261003`](../../scratch/performance-trials/task-callback-cache-20261003).
The tested runtime stayed at
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`.
