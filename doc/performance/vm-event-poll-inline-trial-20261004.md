# Inline VM event polling trial (2026-10-04)

## Result

Rejected. Moving the per-op `interpreter_poll_pending_events()` countdown into the internal header removed an out-of-line call from the VM dispatch loop while retaining same-thread weakref hints and the existing 64-op cross-thread polling bound. It did not improve the 21-pair Pickler-writer workload: candidate/control was **1.0004× slower**, with a 95% interval of **0.9911–1.0186**. The interval spans parity, so this is not a performance win. The source change was reverted and the fixed Release binaries were restored at `build-repro/Release`.

## Validation

`xlang3_interpreter_tests.exe` passed. These event/GC fixtures matched their expected output: `weakref_module`, `gc_class_components`, `gc_rooted_candidate_graph`, `function_attribute_gc`, and `signal_main_thread`.

The full fixture runner was also attempted, but stopped at the unrelated `ctypes_pointer_return` fixture because the current Python 3.14 installation reports `libffi is unavailable for ctypes`. That failure is not counted as a pass.

## Paired evidence

- 21 order-balanced pairs; three warmups; Python 3.14 only.
- Baseline median: **47.088 ms**.
- Candidate median: **47.226 ms**.
- Raw measurements: [`vm-event-poll-inline-pickle-writer-20261004.json`](data/vm-event-poll-inline-pickle-writer-20261004.json).
- Baseline executable/runtime hashes: `F4929AF5873C995AE038A30F5DAF5AB361B321A8FE777F655C7DD3127158E68F` / `E4D81CF016BB520228AFFA495411476698F8274944E25F8261499AB606C53998`.
- Candidate executable/runtime hashes: `75E9AE919B6E4E159A0C772C30E035FDE608E00160CCF1EE12FB75BF6D51A6F4` / `79E98E6F8E6C0693792B68A59ACBA72B866EA80469665312AA6ECF192673800C`.

The complete current-binary 97-definition result is recorded in [`pyperformance-xlang3-final-state-shimmed-vs-cpython314-fast-20261004.md`](pyperformance-xlang3-final-state-shimmed-vs-cpython314-fast-20261004.md). The speed objective remains open. This trial should not be repeated without a materially different implementation hypothesis.
