# asyncio Task registration cache trial (2026-10-03)

> Historical trial: this bound-method cache was removed after a follow-up
> profile found that the `WeakSet.add()` call itself dominated registration.
> CPython 3.14 skips the Python WeakSet for native Tasks and reserves it for
> third-party Tasks; see the [native Task WeakSet-elision trial](asyncio-native-task-weakset-elision-trial-20261004.md).

## Change

Native `_asyncio.Task` registration adds each Task to the registry's
`WeakSet`. Binding `WeakSet.add` dynamically once per Task creates avoidable
lookup and bound-method work on task-heavy workloads. The registry now caches
that bound method only for the exact standard `WeakSet`, and only while its
class version and instance layout remain unchanged. Instance shadows,
replacement classes, attribute hooks, and materialized instance dictionaries
continue through normal dynamic lookup. The cache keeps the WeakSet alive only
as long as the registry already keeps it alive.

This changes no pure-Python `asyncio` implementation. The code runs at XLang3's
native `_asyncio` boundary and preserves Python overrides through its guards.

## Validation

The instance-override fixture passed against the candidate: after priming Task
registration, assigning a replacement `_asyncio._scheduled_tasks.add` was
observed for subsequent tasks. `asyncio_native_call_method_dispatch` and
`asyncio_runtime_edges` also passed.

The full `tests/run_fixtures.py` run stopped at the known unrelated
`ctypes_pointer_return` failure because libffi is unavailable in this build.
It failed before reaching this trial's fixture; the new fixture was run
directly and passed.

## Benchmark

Used the official pyperformance 1.14.0 `async_tree_eager` benchmark in fast
mode with CPython 3.14's benchmark environment and the project's compatibility
shim. Each result below has 20 timed values. Control is the saved pre-build
Release binary; candidate is the Release build with this cache. The executable
path stayed `build-repro/Release/xlang3.exe`.

| Pair | Order | Control | Candidate | Candidate change |
|---|---|---:|---:|---:|
| 1 | control, candidate | 3.000 s ± 0.024 s | 2.982 s ± 0.030 s | 0.6% faster |
| 2 | candidate, control | 3.411 s ± 0.283 s | 3.282 s ± 0.267 s | 3.8% faster |

Both pairs favored the candidate, but the second pair had substantial system
jitter and pyperformance warned that both results were unstable. The geometric
mean of the two pair ratios is about 2.2% faster; treat this as a small,
provisional gain, not a stable headline result. It does not materially close
the large CPython gap by itself. The candidate is retained because the guarded
change is narrow, preserves override behavior, and both comparisons favored
it; future full-suite measurements should confirm its aggregate effect.

## Reproduction artifacts

- `scratch/performance-trials/asyncio-scheduled-tasks-add-cache-20261003/control-r1.json`
- `scratch/performance-trials/asyncio-scheduled-tasks-add-cache-20261003/candidate-r1.json`
- `scratch/performance-trials/asyncio-scheduled-tasks-add-cache-20261003/candidate-r2.json`
- `scratch/performance-trials/asyncio-scheduled-tasks-add-cache-20261003/control-r2.json`

The candidate DLL is also preserved under
`scratch/performance-trials/asyncio-scheduled-tasks-add-cache-20261003/candidate/`.
