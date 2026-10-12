# Asyncio native type identity cache trial (2026-10-05)

## Result

Rejected. Caching `_asyncio.Future` and `_asyncio.Task` in the current
thread's asyncio state did not produce a statistically significant change in
the official pyperformance 1.14.0 `async_tree_none` case. The candidate
measured **5.06 s ± 0.05 s**; its fixed-parent fast run measured
**5.09 s ± 0.06 s**. `pyperf compare_to --table --verbose` hid the result as
not significant. The source change was removed.

This rules out repeated type-attribute imports as a major cause of the async
tree slowdown. The contextual CPython 3.14.7 result is **225 ms ± 10 ms** from
the existing rigorous comparison, so the candidate remained about **22.5×
slower**. This experiment does not materially advance the overall performance
goal.

## Hypothesis and trial

`asyncio_task.cpp::exact_future` loads `_asyncio.Future` and `_asyncio.Task`
from the module on each yielded instance before selecting the native Future
path. The full `async_tree_none` workload creates roughly 56,000 tasks, so
removing repeated module lookups looked like a low-risk way to trim the hot
step path.

The candidate cached both class identities in the existing per-runtime,
per-thread asyncio task state and retained the import fallback when that state
was absent. The official pyperformance worker completed the full selected
`async_tree` definition using CPython **3.14.7** as manager and XLang3's Release
runtime. Both candidate and parent fast-mode files contain 20 measured values
after warmups. The candidate's mean was 0.6% lower, within observed noise; the
statistical comparison did not establish a win.

## Validation

- Full fixture suite: passed.
- Fixed Release regression gate against `build-repro\\perf-control`: all 11
  cases passed; the largest ratio was **1.035×**. See the [gate JSON](data/asyncio-native-type-identity-fixed-release-gate-20261005.json).
- Full Release CTest: **54/55 passed**. The sole failure was the existing
  `xlang3_cli_visual_studio_debugpy_launch` assertion that the Visual Studio
  profile does not use `xlang3.exe` directly.

Raw benchmark files: [candidate](data/async-tree-none-candidate-type-identity-fast-20261005.json),
[fixed-parent control](data/async-tree-none-control-native-stackargs-fast-20261005.json).

The remaining large async gap points toward the per-transition work inside
the task/coroutine execution path, rather than repeated imports of the two
native class attributes.
