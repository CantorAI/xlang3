# Exact native Task constructor trial — 2026-10-04

## Result

Rejected. A guarded full-constructor callback for the exact, unmodified
`_asyncio.Task` class reused the existing native `Task.__init__` callback after
allocating the Future payload. It covered one positional coroutine and the
`loop`, `name`, `context`, and `eager_start` keywords; subclasses, expansions,
duplicate keywords, and patched classes remained on generic construction.
The candidate did not produce a significant `async_tree_eager` improvement,
so the callback and its runtime support were removed. The Python 3.14 Task
construction fixture remains as semantic coverage.

## Matched measurements

The same pyperformance 1.14.0 `async_tree_eager --fast` benchmark ran against
CPython 3.14.7's standard library and dependency site. Control/candidate runs
were ordered control-candidate-candidate-control:

| Build | Mean |
|---|---:|
| Control, run 1 | 2.76 s ± 0.05 s |
| Candidate, run 1 | 2.79 s ± 0.03 s |
| Candidate, run 2 | 2.79 s ± 0.04 s |
| Control, run 2 | 2.80 s ± 0.05 s |

`pyperf compare_to --verbose` found neither candidate run significantly
different from the first control. The reverse-order control was 1.01× slower
than the first control, showing that the observed spread is comparable to the
candidate's roughly 1% difference. This does not support retaining the added
constructor branch. It also does not establish that Task construction is
cost-free; a larger, independently measured hotspot is still needed to close
the roughly 32× async-tree gap.

## Validation and build identity

- The new `asyncio_native_task_constructor.py` fixture produced matching
  output under XLang3 and `C:\Python\Python314\python.exe` 3.14.7. It checks
  eager construction, result/name access, and a subclass initializer.
- `xlang3_interpreter_tests.exe` passed on the compiled candidate.
- After rejecting the optimization, the Release build passed all 11 fixed
  baseline cases with seven order-balanced pairs and two warmups; the largest
  candidate/baseline ratio was `range_for` at 1.006×, below the 1.10 threshold.
- The broad core fixture runner stops at the existing
  `ctypes_pointer_return` failure because libffi is unavailable in this build.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `CC0EE6BFF8057EC8B87D5714B96DEDD8111FB58BA014005242ECB8DFD4BCEB66` | `491942367F67BF264B49C34301BD34A91E31DB64039F40FD6974145CB6C55A5D` |
| Candidate | `CC0EE6BFF8057EC8B87D5714B96DEDD8111FB58BA014005242ECB8DFD4BCEB66` | `CE6C88FEF3DDC85CB75AA9F4067B7996834164508ACA3764452A033490FB5F28` |

## Raw evidence

- [Control run 1](data/async-tree-task-direct-control-r1-20261004.json) and [log](data/async-tree-task-direct-control-r1-20261004.log)
- [Candidate run 1](data/async-tree-task-direct-candidate-r1-20261004.json) and [log](data/async-tree-task-direct-candidate-r1-20261004.log)
- [Candidate run 2](data/async-tree-task-direct-candidate-r2-20261004.json) and [log](data/async-tree-task-direct-candidate-r2-20261004.log)
- [Control run 2](data/async-tree-task-direct-control-r2-20261004.json) and [log](data/async-tree-task-direct-control-r2-20261004.log)
- [Post-rejection fixed Release regression gate](data/asyncio-task-direct-constructor-fixed-baseline-gate-20261004.json)

The overall CPython performance objective remains open. Continue from the
instrumented VM profile and select a target whose ordinary Release A/B can
show a meaningful end-to-end gain; do not repeat this direct Task-construction
shortcut without new evidence.
