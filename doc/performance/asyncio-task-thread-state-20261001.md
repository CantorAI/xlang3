# Asyncio Task-state hot path: CPython comparison

**Status: measured improvement, goal still open.** The native Task path now
keeps the running loop and current Task in per-thread runtime state, matching
CPython's direct interpreter-state lookup. This removes repeated calls through
`asyncio.events` thread-local helpers and `_current_tasks` mapping operations
from each native Task transition. Pure-Python Tasks still use the Python
`_current_tasks` dictionary. The runtime state checks the recorded process ID
so a forked process does not inherit a running loop.

The change is in [`asyncio_module.cpp`](../../src/runtime/modules/system/asyncio_module.cpp).
The comment beside `ThreadTaskState` records why this path stays out of the
Python mapping. The related native GC-edge mirror skip is in
[`asyncio_future.cpp`](../../src/runtime/modules/system/asyncio_future.cpp):
when a Task republishes an unchanged set of owned object references, it leaves
the already-correct collector mirror untouched.

## Official `async_tree_none` result

I compared the preserved native-asyncio binary without per-thread Task state to
the rebuilt candidate. Both sides used pyperformance 1.14.0, the same CPython
3.14.7 dependency site, and the same command-safe Windows compatibility shim.
Each result is a separate official `--debug-single-value` run. Three paired
runs gave these elapsed times:

| Runtime | Run 1 | Run 2 | Run 3 | Median |
|---|---:|---:|---:|---:|
| XLang3 before Task-state change | 6.95 s | 7.13 s | 7.12 s | 7.12 s |
| XLang3 with Task-state change | 5.47 s | 5.54 s | 5.57 s | 5.54 s |
| CPython 3.14.7 | — | 0.233 s | — | 0.233 s |

The paired XLang3 runs improved by **1.28×** at the median. The current
candidate still takes **23.8×** as long as CPython on this case.

```text
async_tree_none elapsed time (left-to-right bars; 0.233 s per block)
CPython 3.14.7  |▏ 0.233 s
XLang3 current  |████████████████████████ 5.54 s
XLang3 before   |███████████████████████████████ 7.12 s
```

These are useful paired diagnostics, not stable `--fast` or `--rigorous`
pyperf distributions. A normal `--fast` run on the preserved control timed out
at the 120-second full-case cap without producing a score; its log is retained
as a failed attempt. Do not treat that timeout as a timing result.

Raw CPython result and log:

- [`pyperformance-cpython314-async-tree-native-await-fastpath-20261001.json`](data/pyperformance-cpython314-async-tree-native-await-fastpath-20261001.json)
- [`pyperformance-cpython314-async-tree-native-await-fastpath-20261001.log`](data/pyperformance-cpython314-async-tree-native-await-fastpath-20261001.log)

Before/after runs:

- [`no-tls-debug-r1`](data/pyperformance-xlang3-async-tree-no-tls-debug-r1-20261001.json), [`tls-debug-r1`](data/pyperformance-xlang3-async-tree-tls-debug-r1-20261001.json)
- [`no-tls-debug-r2`](data/pyperformance-xlang3-async-tree-no-tls-debug-r2-20261001.json), [`tls-debug-r2`](data/pyperformance-xlang3-async-tree-tls-debug-r2-20261001.json)
- [`no-tls-debug-r3`](data/pyperformance-xlang3-async-tree-no-tls-debug-r3-20261001.json), [`tls-debug-r3`](data/pyperformance-xlang3-async-tree-tls-debug-r3-20261001.json)
- [failed `--fast` control log](data/pyperformance-xlang3-async-tree-tls-task-state-control-fast-20261001.log)

## What the profile says next

A level-4 scaled probe with XLang3 counters confirms that the change removes
the repeated mapping work without changing the workload's interpreted bytecode
or native-call count. Bound-method allocations fell from **36,064 to 28,282**;
the probe elapsed time changed from **0.198 s to 0.174 s**. The most frequent
native calls still include 3,378 `Context.run` calls, about 3,100 calls each
to Future `done`, `cancelled`, and `exception`, and roughly 1,550 each to
Future `add_done_callback`, `result`, and awaited-by bookkeeping. The count
report is diagnostic: counters perturb timings and this four-level replica is
not an official pyperformance score.

The native Future-iterator `send` shortcut was compared with the official
case and removed because it measured 6.97 s before versus 7.02 s after. A
cached-type tag experiment was also removed after a 5.51 s versus 5.70 s
sample. The GC-edge unchanged-set check measured 5.47 s versus 5.43 s, only a
1% difference in one-value runs; it remains a small allocation/work avoidance
and is covered by the current full test and regression runs. These results
keep future work focused on measured Task and callback costs rather than
repeating low-yield dispatch experiments.

## Validation and binary identity

- Release CTest: **55/55 passed**.
- Full fixed Release baseline gate: **pass**, all 11 default cases, 21 paired
  samples per case, 10% threshold. Ratios below are candidate time divided by
  baseline time; lower is faster.

  | Case | Ratio | Case | Ratio |
  |---|---:|---|---:|
  | local_slots | 0.0822× | scalar_arithmetic | 0.0221× |
  | range_for | 0.0188× | function_calls | 0.0063× |
  | class_construct | 0.0436× | list_append | 0.0202× |
  | property_access | 0.0041× | deepcopy_memo | 0.5047× |
  | json_dumps | 0.0282× | gc_traversal | 0.1788× |
  | subparsers | 0.6717× | | |

- Full gate report: [`asyncio-task-thread-state-fixed-baseline-20261001.json`](data/asyncio-task-thread-state-fixed-baseline-20261001.json).
- Current executable SHA-256: `3afb3e841a8ba45ae5cb0df05a21fa6390c786709a5dab6b1a11cfaa0906483b`.
- Current runtime DLL SHA-256: `038d28fb6abfa0f4a272df530146672e647cfbe76b95f5bc8f1e0cbb03ccdf55`.
- Fixed baseline executable SHA-256: `3f64142d062d470fba5d8569174b26381a1b92e3a0eabff65a4df09e31348373`.

The relevant CPython 3.14 implementation uses per-thread running-loop and
current-Task fields in `_asynciomodule.c`, including `enter_task`, `leave_task`,
`task_step`, and `task_wakeup`:
[CPython 3.14 `_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c).

This targeted win does not establish an overall pyperformance win. The full
97-case XLang3-versus-CPython comparison still needs a post-change run, and the
`async_tree` family remains substantially slower than CPython.
