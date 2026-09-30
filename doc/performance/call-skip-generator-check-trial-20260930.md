# Skip generator discovery for ordinary Python calls (2026-09-30)

`call_user_function` used to run the generator-construction callback for every
Python call. On ordinary functions that callback only selected the callee's
module, validated its function ID, and read the IR `is_generator` flag before
returning to frame setup, which selected the same module again. The VM now
checks that IR metadata directly and skips the callback on the common valid,
non-generator path, so frame setup reuses the already selected module.
Generator IR and invalid function IDs still use the existing callback;
argument binding, generator creation, and error reporting remain unchanged.
The performance invariant is documented beside the branch in
[`xlang_vm_ops_call.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_call.h).

This changes generic XLang3 call/frame dispatch only. `copy.py`, `pickle.py`,
and the other pure-Python standard-library modules remain Python. The native
module boundary remains limited to modules for which CPython provides a native
module.

## Measurements

Two official pyperformance 1.14.0 fast runs compared the candidate with the
immediate pre-change Release runtime. The first run covered `deepcopy`,
`pickle_pure_python`, and `unpickle_pure_python`; the reverse-order repeat
covered both serialization benchmarks:

| Benchmark | Control, merged | Candidate, merged | Result |
| --- | ---: | ---: | ---: |
| `pickle_pure_python` | 6.59 ±0.29 ms | 6.43 ±0.12 ms | 1.02× faster, significant (`t=3.20`) |
| `unpickle_pure_python` | 4.43 ±0.10 ms | 4.36 ±0.07 ms | 1.02× faster, significant (`t=3.53`) |
| `deepcopy_memo` | 571 ±46 μs | 558 ±5 μs | 1.02× faster, not significant |

Several fast runs warned that their samples were not stable enough to meet the
1% variation target. The paired serialization gains are small but appeared in
both orders; the `deepcopy` result does not establish an improvement. All
11 cases in the fixed-baseline Release gate passed, and the complete Python
fixture suite passed.

The saved CPython 3.14.7 full-fast measurements were **252 μs** for
`pickle_pure_python` and **161 μs** for `unpickle_pure_python`. XLang3 remains
about **25×** and **27× slower**, respectively. This call-path reduction is
incremental and does not close those gaps.

## Evidence

- Raw pyperf runs: [control, first order](data/call-skip-generator-probe-control-fast-20260930.json), [candidate, first order](data/call-skip-generator-probe-candidate-fast-20260930.json), [control, reverse-order repeat](data/call-skip-generator-probe-control-repeat-fast-20260930.json), and [candidate, reverse-order repeat](data/call-skip-generator-probe-candidate-repeat-fast-20260930.json)
- Combined pyperf runs: [control](data/call-skip-generator-probe-control-merged-fast-20260930.json) and [candidate](data/call-skip-generator-probe-candidate-merged-fast-20260930.json)
- Fixed-baseline Release gate: [11-case report](data/call-skip-generator-check-fixed-baseline-20260930.json)
- CPython 3.14.7 reference: [saved full-fast results](data/pyperformance-cpython314-full-fast-20260928.json)

The control runtime DLL SHA-256 was
`17244BF749C172422F79C7AE1C705CF2A6B6A053239570C178C5C839E02E4D5F`; the
candidate runtime DLL SHA-256 was
`20E542F6186D6406904F223BBA42B0F6E7E072A64A9C3231DBD4F108F290BDAD`.
