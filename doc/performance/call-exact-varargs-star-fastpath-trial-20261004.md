# Rejected exact starred varargs binding fast path — 2026-10-04

## Result

Rejected: the exact tuple/list `*args` binding fast path did not show a repeatable speedup. An early candidate sample measured **2.84 s ± 0.02 s** against an earlier **2.94 s ± 0.03 s** control, but a source-matched run measured the parent at **2.82 s ± 0.01 s** and the candidate at **2.87 s ± 0.04 s**. Pyperf flagged the candidate as unstable. I removed the fast path rather than keep code whose direction reversed across runs.

The fast-path candidate passed the fixed Release gate with all 11 cases, the slowest being `subparsers` at **1.093×** the preserved baseline. After removing the performance path and keeping only the empty-tuple identity correction, the final source also passed all 11 cases; `subparsers` measured **1.090×**. The full fixture suite and `xlang3_interpreter_tests` passed on the final source. The new call-expansion fixture remains as CPython compatibility coverage.

## Profile and change

An opt-in native VM timing probe, built separately from the preserved executable, attributed **36.17%** of positive measured self-time to `CallEx` on this workload (about **1.172 s per tree** in the instrumented profile). This is diagnostic attribution, not an ordinary-build timing or a CPython comparison. The eager tree's recursive `asyncio.gather(*children)` calls compile to `CallEx`; each call binds a Python varargs-only function from a single starred list.

The generic function binder expanded that list into `expanded_positional`, copied those values into `extra_positional`, then built the required `*args` tuple. The guarded fast path creates the final tuple directly from one exact tuple/list star operand when the target has only `*args`, with no explicit positional arguments, keyword expansion, keywords, or defaults. It reuses CPython's empty-tuple singleton case and copies non-empty tuples, matching observed `CALL_FUNCTION_EX` identity behavior. Subclasses and arbitrary iterables stay on the generic path so their iteration hooks still run.

The list/non-empty tuple performance path and its comment were removed from `src/executor/xlang_vm/xlang_vm_loop.cpp`. The experiment's fixture exposed a separate compatibility bug: XLang3 returned a newly allocated empty `*args` tuple where CPython reuses the exact empty tuple passed through `CALL_FUNCTION_EX`. A narrow empty-tuple identity fix remains, documented beside the binder; it is a semantic correction and carries no claimed benchmark gain. The `call_ex_varargs_star_fastpath` fixture checks this behavior along with non-empty tuple/list identity, list-subclass iteration, multiple stars, and keyword expansion against CPython 3.14. No Python standard-library source was replaced or moved into C++.

## Measurements and raw evidence

| Runtime/build | `async_tree_eager` mean |
| --- | ---: |
| XLang3 immediately before this change | 2.94 s ± 0.03 s |
| XLang3 source-matched parent | 2.82 s ± 0.01 s |
| XLang3 fast-path candidate (rejected) | 2.87 s ± 0.04 s |
| CPython 3.14.7 | 86.4 ms ± 1.4 ms |

The source-matched pyperf comparison reports the candidate **1.02× slower** than its parent. The rejected candidate is about **32.93× slower** than CPython; the current parent remains about **32.6× slower**. All runs used pyperformance 1.14.0 and Python 3.14.7 dependencies. Raw data and logs are:

- XLang3 candidate: `data/async-tree-call-exact-varargs-candidate-fast-20261004.json` and `.log`
- XLang3 source-matched parent: `data/async-tree-call-exact-varargs-control-fast-r1-20261004.json` and `.log`
- XLang3 second candidate run: `data/async-tree-call-exact-varargs-candidate-fast-r2-20261004.json` and `.log`
- CPython reference: `data/async-tree-call-exact-varargs-cpython314-fast-20261004.json` and `.log`
- Prior XLang3 control: `data/async-tree-future-new-type-cache-candidate-20261004.json`
- Fixed-baseline gate: `data/release-regression-call-exact-varargs-20261004.json`
- Final-source fixed-baseline gate: `data/release-regression-call-exact-varargs-final-20261004.json`
- Instrumented diagnostic profile: `data/async-tree-vm-timing-delta5-20261004.json` and `.csv`; raw processes are in `scratch/performance-trials/async-tree-vm-timing-20261004/`.

Tested candidate executable SHA-256: `aced403d9b828e838e28bd739f604b4d5e22ac13b481a0f9b9c344831529d7ae`.
Tested candidate runtime DLL SHA-256: `30d863b4963ba22d3626560224dc6deda676f4f0df693daf30b1804b23151cc6`.
Final-source executable SHA-256: `88af60fe5ae376ffb6feddca6cd50fd9fdcc926dbb9e48762daef78bdb764671`.
Final-source runtime DLL SHA-256: `46dff70b4a974f950d9f6f90a0738a55278b1f1bfdb195c00be0bf8da3004856`.
The fixed baseline executable remains `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.

The next measured target is the remaining Python call and method-dispatch time in the same VM profile. The overall pyperformance objective remains unmet.
