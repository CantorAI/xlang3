# Native asyncio current-task thread-state fast path (2026-10-06)

## Result

The native Task transition now follows CPython 3.14's per-thread current-task
design. It no longer mirrors every native Task enter/leave into the Python
`asyncio.tasks._current_tasks` fallback dictionary. When the native thread
state says no Task is active and that exact dictionary is empty, the runtime
returns `None` directly instead of calling `dict.get(loop)`; a nonempty mapping
continues through the normal lookup path for Python-managed Tasks.

Official pyperformance 1.14.0 measured `async_tree_none` **1.23× faster**:
**3.66 s ± 0.05 s** versus **4.49 s ± 0.04 s** on the fixed control. The
candidate run emitted pyperf's low-sample stability warning, so the ratio is
directional. Against the saved CPython 3.14.7 result of 227.4 ms, this reduces
the case from about 19.8× to about 16.1× CPython's time. XLang3 remains much
slower on this case and the overall goal is open.

| Runtime/build | `async_tree_none` |
| --- | ---: |
| CPython 3.14.7 saved full-run reference | 227.4 ms |
| XLang3 fixed Release control | 4.49 s ± 0.04 s |
| XLang3 current-task thread-state path | 3.66 s ± 0.05 s |

## Why this path is safe

CPython 3.14's native `_asyncio` tracks the running Task in thread state; its
native Task path does not write the Python fallback dict. XLang3's registered
`_asyncio.current_task`, `_enter_task`, `_leave_task`, and `_swap_current_task`
use the same native state. Keeping a second copy in `_current_tasks` performed
two extra dictionary mutations per Task transition and invoked a custom event
loop's `__hash__` even though CPython does not.

The shortcut only returns a miss when the current loop matches the thread
state's loop and the exact fallback dict is empty. If it contains an entry,
the runtime still performs normal Python mapping lookup, preserving
Python-managed Task behavior. A parity fixture checks that a custom-hash event
loop sees no hash calls during native Task execution, that the fallback dict
stays empty, and that both `asyncio.current_task()` APIs return the same Task
under XLang3 and CPython 3.14.7.

An environment-gated diagnostic over 65,320 `async_tree_none` Task steps on
the parent build measured 521.4 ms in current-task lookup and 345.9 ms in
Python mapping updates. Those timers are diagnostic totals, not ordinary-build
scores; they identified redundant work in the native Task path. The
performance comment beside `current_for_loop` records the guard and fallback
conditions.

## Validation

- `xlang3_runtime_value_tests` passed.
- `xlang3_interpreter_tests` passed.
- The full `tests/run_fixtures.py` suite passed using
  `C:\Python\Python314\python.exe` and the complete Release directory.
- The custom-loop hash parity fixture produced identical output on CPython
  3.14.7 and XLang3.
- The fixed Release regression gate passed all 11 cases with 21
  order-balanced paired samples and five warmups. Candidate/control medians
  were between 0.984× and 1.014×.

The fixed control executable/runtime DLL hashes are
`E8EFEE922E95093437E0FEF3F6754ED2730A3CEC2BBA9A4C067F2C660C7B202C` and
`B29EE2944F3916F7D901EB2A178F58CD9EA1B36DC1D1DFB2033338012D61CC2D`.
The candidate hashes are
`A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF` and
`BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D`.

Both pyperformance runs used CPython **3.14.7** as manager, the same shared
Python 3.14 site-packages, and the repository's Windows compatibility shim.
Raw results and the complete regression gate are preserved here:

- [Control pyperf JSON](data/async-task-thread-state-control-fast-20261006.json)
- [Candidate pyperf JSON](data/async-task-thread-state-candidate-fast-20261006.json)
- [pyperf comparison](data/async-task-thread-state-compare-fast-20261006.txt)
- [21-pair fixed Release gate](data/release-async-task-thread-state-gate-20261006.json)

This change does not implement a C++ version of `asyncio`; it removes overhead
from XLang3's `_asyncio` native module while retaining Python's standard
library code and its fallback semantics.
