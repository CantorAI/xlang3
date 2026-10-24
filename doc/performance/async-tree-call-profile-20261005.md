# Default async-tree call profile (2026-10-05)

This diagnostic compares the unchanged `async_tree_scaled.py` workload at
four levels, three branches, and one iteration under CPython 3.14.7 and a
fresh Release build of current `main`. It counts Python call events in
`asyncio` and visible `_asyncio` C-call events; it is not a timing comparison.

| Runtime | Python `asyncio` call events | `_asyncio` C-call events visible to `sys.setprofile` |
|---|---:|---:|
| CPython 3.14.7 | 2,947 | 335 |
| XLang3 | 2,984 | 0 |

The Python-frame counts differ by only 37 (1.3%). The largest shared Python
call counts are `BaseEventLoop.get_debug` (458), `_check_closed` (421),
`call_soon` / `_call_soon` (292 each), `Handle.__init__` (292), and
`Handle._run` (291). XLang3 reports 44 calls to
`asyncio.futures._get_loop`, compared with 4 under CPython. In this tree there
are 40 gather-created Future objects; CPython's native Task path calls the
Future's `get_loop` method from `_asyncio` C code, while XLang3's generic
fallback enters the Python `_get_loop` helper. The `_asyncio` C-call totals
are not directly comparable: the profile callback filters on
`arg.__module__ == "_asyncio"`, and the two runtimes expose different native
callable metadata.

This confirms a call-boundary difference, not a timing cause. The same class of
fallback was already tested: XLang3's native Task resolved `get_loop()`
directly for non-exact Future objects, reducing helper calls in an eager-tree
probe, but a source-matched official `async_tree_eager` comparison measured
no significant speedup (1.41 s for both builds). Do not repeat that exact
fallback change without a new optimization that removes meaningful work from
the native-call path.

The same fresh Release build's VM counters recorded 23,685 native calls
(21,377 fast, 5,660 cached-fast), including 291 `_contextvars.Context.run`
calls (165 generic and 126 fast) for the 4x3 tree. These counters show the
empty-star `Context.run` path is active, but many non-empty callback expansions
still use the generic path. A broader expansion shortcut was already tried
and measured slower in the scaled workload, so it is not a new candidate. See
the [counter log](data/async-tree-scaled-counters-xlang3-20261005.txt).

A separate current-source fixture probe confirms the Future fast-keyword
callback is enabled: the `asyncio_native_future_constructor.py` fixture
records `_asyncio.Future.__init__ slow=1 fast=2`. See the
[counter probe](data/future-fast-keyword-counter-probe-xlang3-20261005-rebuilt.txt).

CPython 3.14.7 calls the built-in Future method directly in
[`_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c#L294-L315).
The XLang3 fallback and prior A/B result are documented in the
[Task loop-lookup trial](asyncio-task-loop-lookup-trial-20261005.md).

Reproduction:

```powershell
C:\Python\Python314\python.exe benchmarks\diagnostics\async_tree_call_profile.py --levels 4 --branches 3 --iterations 1
$env:XLANG3_PYTHON_LIB = 'C:\Python\Python314\Lib'
$env:PYTHONPATH = 'benchmarks\diagnostics;C:\Python\Python314\Lib;C:\Python\Python314\Lib\site-packages'
.\build-repro\Release\xlang3.exe benchmarks\diagnostics\async_tree_call_profile.py --levels 4 --branches 3 --iterations 1
```

Raw output: [CPython 3.14.7](data/async-tree-call-profile-cpython314-20261005.txt),
[XLang3](data/async-tree-call-profile-xlang3-20261005.txt). The XLang3
executable and runtime DLL SHA-256 hashes were
`091105B9328CC1D1531E2E70FB86B9FDC23A8608E3BE8DDCBAD90BDB532A8B3F` and
`1ED6DA293DED131E3EB07C4C019D0F3D858E0EE3815A1A90B6EF2B5E410C8486`.
