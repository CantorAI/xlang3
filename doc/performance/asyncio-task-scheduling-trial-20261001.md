# Asyncio Task stepping investigation (2026-10-01)

The full pyperformance run timed out every `async_tree` variant. CPython 3.14.7
reports `asyncio.tasks.Task` and `asyncio.futures.Future` as native
`_asyncio.Task` and `_asyncio.Future`. XLang3 reports the Python classes
`asyncio.tasks.Task` and `asyncio.futures.Future`; it has no registered native
`_asyncio` module. The Python Task implementation runs a substantial
`__step_run_and_handle_result()` path for each coroutine resume.

This source comparison makes `_asyncio` Task/Future execution a credible target
under the project rule: CPython itself implements these classes natively. It
does not by itself prove that replacing the missing accelerator will fix the
timeouts; the Task/Future scheduling path needs a focused native profile and
semantic coverage before committing an implementation.

## Rejected generator-send fast path

CPython's native Task uses `PyIter_Send` to advance its coroutine. A candidate
VM path called XLang3's existing generator-send implementation directly for
`generator.send(value)` when tracing, profiling, monitoring, and debugging were
inactive. It retained the existing generic method path for observable calls.
The candidate was removed after testing because it showed no repeatable gain.

The official pyperformance `coroutines` case measured **150 ± 13 ms** for the
control and **157 ± 13 ms** for the candidate. `pyperf compare_to` reports the
candidate **1.05× slower**. The samples were unstable, so this is not evidence
of a real regression, but it provides no reason to keep the added fast path.
The control [JSON](data/coroutines-send-control-rigorous-20261001.json) and
[log](data/coroutines-send-control-rigorous-20261001.log) and the candidate
[JSON](data/coroutines-send-candidate-rigorous-20261001.json) and
[log](data/coroutines-send-candidate-rigorous-20261001.log) preserve the raw
runs.

The official `async_tree` candidate probe was still running at 90 seconds and
used about 3.3 GB of private memory without returning a measurement. I stopped
that probe to contain its memory growth. The full suite's earlier control run
also timed out on `async_tree`; neither run supplies a timing for this case.
The stopped candidate's [log](data/async-tree-send-candidate-fast-20261001.log)
records the timeout.

## Scaled scheduling probe

The parameterized [scaled tree diagnostic](../../benchmarks/diagnostics/async_tree_scaled.py)
repeats the same gather-shaped task tree at a smaller size. For 30 trees with
4 levels and 3 branches, three runs produced:

| Runtime/build | Times (seconds) | Median |
|---|---:|---:|
| CPython 3.14.7 | 0.02945, 0.02952, 0.02898 | 0.02945 |
| XLang3 control | 1.24292, 1.45696, 1.08093 | 1.24292 |
| XLang3 direct-send candidate | 1.28786, 1.28946, 1.34409 | 1.28946 |

XLang3 is about **42× slower** than CPython on this small diagnostic. The
candidate is about **3.7% slower** than its XLang3 control median, which is
within the screening run's noise and does not establish a meaningful gain.

With performance counters enabled, one XLang3 control run reported 19,225
generic `Call` dispatches, 13,710 `CallMethod` dispatches, 8,243 `BoundMethod`
allocations, 10,767 tuple allocations, and 3,960 instance allocations. These
counters include module imports and workload setup, so they are diagnostic
counts rather than per-task costs. The complete output is in
[`async-tree-scaled-perf-counters-20261001.txt`](data/async-tree-scaled-perf-counters-20261001.txt).

The next experiment should target the native-compatible Task/Future state and
resume/scheduling path, then rerun this scaled workload and the official
`async_tree` case. The full-suite report remains
[here](pyperformance-xlang3-await-inline-full-fast-20261001.md).
