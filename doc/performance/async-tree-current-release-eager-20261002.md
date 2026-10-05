# Current Release `async_tree_eager` result (2026-10-02)

The current Release executable completed the official pyperformance 1.14.0
`async_tree_eager` benchmark when the per-definition cap was raised from 120
to 300 seconds. The shorter-cap attempt timed out before emitting a result;
that alone did not prove a single tree execution was stuck. The longer run
measured **3.66 s ± 0.13 s** and pyperf marked the sample unstable. Against
the saved CPython 3.14.7 result of **86.6 ms**, XLang3 is **42.25× slower**.
XLang3 still loads the accessible Python 3.13 standard library, so the runtime
and standard-library versions are not fully matched.

The run used executable SHA-256
`5A6C3FECC9150CA3B546411C242E7207735929A7E33FEFB667CE9E4E559083FB` and
runtime DLL SHA-256
`1D355E70D6BA57696217E274664A0AB72C4C2F51ABCE3CE6DEC48D528A92E90E`.
Raw results: [current XLang3 run](data/async-tree-current-release-eager-fast-20261002-r2.json)
and [CPython 3.14.7 reference](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).

The direct repeated-loop diagnostic reproduced pyperformance's lifecycle
using the exact official `EagerAsyncTree` object and source. Nine full trees
completed on fresh event loops, with a **3.62 s median** (one sample at
3.91 s); the saved per-run log is
[`async-tree-current-release-repeats-20261002.txt`](data/async-tree-current-release-repeats-20261002.txt).
This run did not reproduce the earlier intermittent no-progress stall, whose
root cause remains unresolved. The Release `_asyncio` current-task/TaskGroup
fixture passes on this build. That compatibility fix does not close the
measured async performance gap.
