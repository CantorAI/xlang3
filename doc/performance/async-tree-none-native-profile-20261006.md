# `async_tree_none` native profile (2026-10-06)

## Finding

The safe-looking `LoadLocalAttr` shortcut was invalid: later IR can read the
temporary receiver register (`in.c`), including the `StoreAttr` in an augmented
assignment. Skipping that write made asyncio loop shutdown fail. The candidate
was removed, the Release binary was rebuilt from the restored source, and the
full Python 3.14.7 fixture runner passed.

Two separate diagnostics help narrow the remaining gap, but neither identifies
a single cause of the roughly 4.5-second run. A user-mode sample of the exact
pyperformance `NoneAsyncTree` body found frequent `Value` release/copy work,
string-key hashing, and `object_get_attr`. A timer-instrumented pyperformance
run attributed only a few percent of its measured VM-op time to each of
`Call` and `CallMethod`; the tested `LoadLocalAttr` op contributed about
2.8 ms in that diagnostic run. Those opcode timings cannot explain the full
wall-time gap, so they are not a basis for claiming a speedup.

The earlier instrumented run did not record XLang3's native Task-step timers.
That absence was not evidence of a different Task-step path: a follow-up
observer around the event loop's `call_soon` confirmed that this benchmark
schedules the native `_asyncio.Task._step` callback 55,989 times. The reason
the timer probe missed it remains unresolved; do not use those missing timer
rows to rule out Task dispatch as a hotspot.

## Pyperformance context

The fresh official pyperformance 1.14.0 fast control measured
`async_tree_none` at **4.50 s ± 0.04 s**. The saved CPython 3.14.7 result in the
97-case comparison is **227.4 ms**, putting XLang3 at about **19.8× the time**
on this case. The CPython result is the saved full-run reference, not a newly
paired run from this diagnostic session.

| Runtime / diagnostic | Time | Meaning |
| --- | ---: | --- |
| CPython 3.14.7 | 227.4 ms | Saved official fast-mode reference |
| XLang3 Release control | 4.50 s ± 0.04 s | Fresh official pyperformance fast run |
| XLang3 timing-probe run | 4.51 s | Debug-mode score with profiling enabled; diagnostic only |

## Native sample points

The sampler captured **1,160** user-mode instruction-pointer samples during
one direct execution of the official benchmark body using
[`run_async_tree_none_body.py`](../../benchmarks/diagnostics/run_async_tree_none_body.py);
**786** landed in
`xlang3_runtime.dll`. Bars show sample counts, not CPU-time percentages. The
sample is useful for choosing targets, but it suspends the process and does
not provide call stacks or precise time attribution.

![Horizontal native sample counts for the async tree workload.](async-tree-none-native-profile-20261006.svg)

| Sampled symbol | Samples in runtime DLL |
| --- | ---: |
| `release(Value)` | 52 |
| MSVC string hash bytes | 38 |
| `unordered_map` string lookup | 28 |
| `Interpreter::run_function` | 26 |
| `object_get_attr` | 22 |
| `call_method` | 18 |
| `Value::operator=` | 17 |
| string equality | 16 |
| `gc_untrack_object` | 15 |

The [raw sample data](data/async-tree-current-native-samples-20261006.json)
contains module addresses and sample counts. The corresponding
[symbolizer output](data/async-tree-current-native-symbols-20261006.txt)
preserves the source locations.

## Scheduled Task callbacks

[`async_tree_call_soon_profile.py`](../../benchmarks/diagnostics/async_tree_call_soon_profile.py)
wraps `BaseEventLoop.call_soon` and counts callback types scheduled by the
official `NoneAsyncTree` body. It was run once under each runtime; wrapping the
event-loop method changes timing, so these are counts only.

| Scheduled callback | XLang3 | CPython 3.14.7 |
| --- | ---: | ---: |
| Task step | 55,989 `_asyncio.Task._step` | 55,989 `_asyncio.Task.TaskStepMethWrapper` |
| Task wakeup | 9,331 `_asyncio.Task._wakeup` | 9,331 `_asyncio.Task.task_wakeup` |
| `gather` done callback | 55,986 | 55,986 |
| Total `call_soon` schedules | 121,315 | 121,315 |

The workload creates the same number of scheduled operations under both
runtimes. CPython represents the task step with its dedicated native wrapper;
XLang3 schedules a bound native method. This makes callback invocation and
bound-method allocation worth measuring next, while leaving open the cost of
coroutine resume and Python `Handle` execution. The matching schedule counts
also show that the missing timer records came from the probe setup, not from
the benchmark bypassing native Task steps.

## Opcode timing diagnostic

The timer probe ran the official `async_tree_none` body once in debug mode with
scoped timers around each VM opcode. A clock pair averaged 14 ns. The busiest
timed operations were imports and calls; individual local-load operations
were inexpensive relative to total wall time.

| Opcode | Calls | Self time |
| --- | ---: | ---: |
| `ImportModule` | 403 | 55.2 ms |
| `ImportFrom` | 610 | 40.8 ms |
| `Call` | 19,895 | 36.7 ms |
| `CallMethod` | 9,113 | 15.0 ms |
| `CallModuleMethod` | 555 | 14.6 ms |
| `CallLocalMethod` | 4,823 | 5.5 ms |
| `LoadLocalAttr` | 21,270 | 2.8 ms |

These are instrumented self-times, not ordinary-build costs. The raw
[timing log](data/async-tree-none-opcode-timing-20261006.log) retains every
opcode row. The probe source was removed before rebuilding the ordinary
Release binary.

## Rejected candidates and validation

An inline no-argument self-attribute getter measured three debug single-value
samples at 4.48, 4.42, and 4.37 s, against controls at 4.43, 4.37, and 4.42 s.
The distributions overlap; no change was retained. The raw control and
candidate files are [control 1](data/pyperformance-async-tree-self-attr-getter-control-debug-20261006.json),
[control 2](data/pyperformance-async-tree-self-attr-getter-control-debug-r2-20261006.json),
[control 3](data/pyperformance-async-tree-self-attr-getter-control-debug-r3-20261006.json),
[candidate 1](data/pyperformance-async-tree-self-attr-getter-candidate-debug-r1-20261006.json),
[candidate 2](data/pyperformance-async-tree-self-attr-getter-candidate-debug-r2-20261006.json), and
[candidate 3](data/pyperformance-async-tree-self-attr-getter-candidate-debug-r3-20261006.json).

The `LoadLocalAttr` shortcut was removed after the official fast benchmark
failed in loop shutdown and the full fixture runner failed on a subprocess
fixture. The restored Release executable passed the full fixture suite using
`C:\Python\Python314\python.exe tests/run_fixtures.py
build-repro/Release/xlang3.exe`. No engine optimization from these two trials
remains in the source. The Release control distribution is
[preserved here](data/pyperformance-async-tree-loadlocalattr-control-fast-r1-20261006.json);
the candidate's single debug value was 4.488 s, but it is not comparable to the
fast distribution. The candidate debug result is
[preserved here](data/pyperformance-async-tree-loadlocalattr-candidate-debug-20261006.json),
and the [failed candidate run log](data/pyperformance-async-tree-loadlocalattr-candidate-fast-failure-20261006.log)
shows the asyncio shutdown failure.

| Build | SHA-256 |
| --- | --- |
| Ordinary Release executable | `AA0FBB7F22F970D2809611A126D8B5780A46C777EC7E23204EE06BFABB559D64` |
| Ordinary Release runtime DLL | `F165447357BFD9369D873B2F5DDB86650D985300F88D160681E5D1D51652E9E1` |
| RelWithDebInfo sample executable | `ADDE6540D99DFDE45B725CD076503BB0E9923738A16B61381CDEA809194ACA56` |
| RelWithDebInfo sample runtime DLL | `7398591CF9F28E46B6BB0A29890411EF35CD33CAF8FDA4BE379266B335A63B2A` |

The next experiment should isolate native Task callback invocation and shared
VM call/frame work without changing callback semantics. The performance goal
remains open.
