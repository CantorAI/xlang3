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

## Current full-suite calibration observation

The refreshed set/regex candidate run has a 60-second cap for each complete
benchmark definition. An observed `async_tree_memoization_tg` child was a
pyperf `--worker --calibrate-loops --values 2 --min-time 0.1 --warmups 1`
process and had accumulated more than 11 seconds of user CPU time. The
`asyncio_tcp` definition subsequently also had a live calibration worker.
These observations confirm active workers, rather than a stopped suite.
They do not prove that a benchmark is semantically stuck. The timeout covers
calibration and all measurement workers together, so it is not itself a
steady-state timing or evidence of one specific runtime defect.

The new [call-event diagnostic](../../benchmarks/diagnostics/async_tree_call_profile.py)
uses the existing scaled tree and counts Python asyncio call events by function.
After the suite and the SSL validation runs terminated, it ran under both
runtimes: 5,742 Python asyncio call events for CPython and 12,177 for XLang3,
with matching shared event-loop callback counts. Native Task internals do not
produce equivalent Python events, so the counts identify Python scheduling
work rather than supply a directly comparable instruction or time metric.
The [completion/scheduling source comparison](windows-iocp-and-task-source-comparison-20261001.md)
records both raw traces and the separate native Windows completion diagnostic.

CPython 3.14.7's
[`Modules/_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c)
keeps Future state, result/exception, loop, and the first callback/context in
native fields. Task extends that state with the coroutine, waiter, context,
and cancellation counters. That is a materially broader target than the
rejected standalone generator-send shortcut. A future XLang3 implementation
must preserve callback order/context, cancellation and exception propagation,
subclasses and custom awaitables, eager execution, task introspection, and
observable tracing/monitoring behavior.

XLang3 already provides `instance_set_native_gc_references` and a native clear
callback for tracing references held by native payloads. A native Task/Future
implementation must update those edges as waiters, callback lists, results,
and exceptions change, so task/future/callback cycles remain collectible.
The Python event-loop implementation stays Python; `_asyncio` is the allowed
native accelerator boundary, matching CPython's import name and public API.
