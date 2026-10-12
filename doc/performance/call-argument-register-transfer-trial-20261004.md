# Call argument register ownership transfer trial — 2026-10-04

## Result

No measurable performance gain. The guarded transfer path is retained as a
semantic correction: when a caller register is dead at a direct, fixed-arity
Python call, ownership of its argument can move into the callee frame. This
makes object finalizers run at the same point as CPython 3.14 for the tested
last-use call shape. Do not cite this trial as a pyperformance speedup.

The transfer is restricted to 1–8 exact positional arguments, no binding,
keywords, expansion, or leading arguments, a simple fixed signature, and
caller registers whose last use is the active call instruction and which are
not loop-carried. Duplicate registers, live values, observable/debugged
frames, and every uncertain case use the existing copy path. The fixture
checks finalizer timing and repeated use of the same value in two argument
positions. Its perf counter confirms two transfers for the test.

## Matched Python 3.14 measurements

pyperformance 1.14.0 fast mode used `C:\Python\Python314\python.exe` 3.14.7,
the same benchmark dependency site, and order control-candidate-candidate-
control. The `pyperf compare_to --verbose --table` result found no significant
change in any of the three workloads. `async_tree_eager` was 2.82 s in each
control and 2.85 s in each candidate; the comparison reports the second
candidate at 1.01× slower. Pure-Python pickle and telco were hidden as
not-significant results.

| Workload | Control 1 | Candidate 1 | Candidate 2 | Control 2 | Result |
|---|---:|---:|---:|---:|---|
| `async_tree_eager` | 2.82 s ± 0.03 s | 2.85 s ± 0.05 s | 2.85 s ± 0.03 s | 2.82 s ± 0.03 s | No significant change; candidate 2 is 1.01× slower |
| `pickle_pure_python` | 5.27 ms ± 0.35 ms | 5.36 ms ± 0.67 ms | 5.31 ms ± 0.43 ms | 5.16 ms ± 0.05 ms | Not significant |
| `telco` | 187 ms ± 14 ms | 192 ms ± 12 ms | 193 ms ± 14 ms | 188 ms ± 6 ms | Not significant |

## Validation

- The fixture prints `inside 1`, `after 1`, `pair-inside 1`, `pair-after 1`
  with XLang3 and Python 3.14.7. The saved pre-change XLang3 control prints
  zeros at the corresponding points, demonstrating the object-lifetime
  mismatch this guarded ownership transfer corrects.
- `xlang3_interpreter_tests.exe` passes; the fixture reports exactly two
  transfers with `--perf-counters`.
- The 11-case Release regression gate passes with seven repeats, two warmups,
  and a 10% threshold. The slowest candidate/baseline ratio is `list_append`
  at 1.019×.
- Candidate executable SHA-256:
  `00133F5E28BA8E6565F8447A3C758D7096F8906EF3401847407E175D44FF0E0A`.
- Candidate runtime DLL SHA-256:
  `381198090024EE4236BA87764C62D19DC660149CD619AC5595F3CC24FC37856F`.
- Control executable SHA-256:
  `CC0EE6BFF8057EC8B87D5714B96DEDD8111FB58BA014005242ECB8DFD4BCEB66`.
- Control runtime DLL SHA-256:
  `491942367F67BF264B49C34301BD34A91E31DB64039F40FD6974145CB6C55A5D`.

## Raw evidence

Four pyperformance JSON files and logs are in `data/` with the
`call-argument-register-transfer-*20261004` prefix. The local fixed Release
gate result is `data/call-argument-transfer-fixed-baseline-gate-20261004.json`.

## Exact `CallMethod` argument transfer follow-up

The same last-use ownership rule now covers exact cached method calls. Their
receiver is a leading argument, so the callsite passes its register index
explicitly; the transfer still requires every receiver/argument register to be
dead at the current instruction, not loop-carried, and unique. This restores
CPython 3.14 finalizer timing when a temporary object is passed to a method and
deleted inside that method. The fixture covers both a temporary argument and
repeated references to the same object.

This is a correctness fix, not a performance win. On the final safe Release
build, 21 order-balanced pairs measured `subparsers` at **1.0095× slower**
(95% interval **0.9984–1.0151**) and the Pickler-writer workload at **1.0050×
slower** (**0.9998–1.0172**). Both intervals include parity; neither shows a
speedup. Do not count this change toward the performance objective.

- [Final safe subparsers run](data/callmethod-register-transfer-subparsers-safe-20261004.json)
- [Final safe Pickler-writer run](data/callmethod-register-transfer-pickle-writer-safe-20261004.json)
- [Earlier subparsers screening run 1](data/callmethod-register-transfer-subparsers-ab-20261004.json)
- [Earlier subparsers screening run 2](data/callmethod-register-transfer-subparsers-ab-r2-20261004.json)
- [Earlier Pickler-writer screening run](data/callmethod-register-transfer-pickle-writer-ab-20261004.json)

`xlang3_interpreter_tests.exe` and the argument-transfer, method-shadow, and
attribute-precedence fixtures passed after the change. The argument-transfer
fixture now agrees with Python 3.14 for temporary arguments deleted inside
both direct functions and methods.

The overall CPython speed objective remains open. Continue from current
profiles, and only call a change a performance win after repeated matched
Release runs show a significant end-to-end improvement.
