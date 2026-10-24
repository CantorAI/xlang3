# Outlining the VM `CallMethod` handler (2026-10-03)

## Result

Rejected. Marking the large templated `call_method` handler `noinline` did not
produce a repeatable improvement, so the original inline policy is restored.
Across two independent 21-pair order-balanced comparisons, `subparsers` first
measured candidate/control **0.9891×** (95% interval **0.9813–1.0030**) and
then **1.0050×** (**0.9980–1.0104**). Neither interval excludes parity. A
21-pair `function_calls` comparison measured **0.9981×** (**0.9579–1.0619**),
also inconclusive. All runs checked identical output.

The experiment tested whether reducing hot dispatch code footprint would
outweigh one out-of-line call per `CallMethod`. This build-level hypothesis is
not supported by these workloads; no source change from the trial remains.

## Build identity and method

Both binaries were built from the same current working-tree snapshot with
MSVC Release `/O2 /Ob3`; only the `call_method` inline annotation differed.
The comparisons used the fixed Python 3.14.7 interpreter environment and
`benchmarks/diagnostics/measure_case_pair.py` with five warmups and 21
order-balanced pairs per run.

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Control | `F04A35B4DBB32724431E9AA5803B1B966F56251539037BCABF4B5C3CF540A39E` | `452882D8DE64D5AA4AC8636E19820D66154F04B903DCBFF23906BFB55B5CF7B8` |
| Candidate | `F04A35B4DBB32724431E9AA5803B1B966F56251539037BCABF4B5C3CF540A39E` | `2F75CD51615B4822512C9B81979E813748664BA443B27098565CD1258D52118D` |

Raw paired measurements: [subparsers run 1](data/callmethod-outline-subparsers-20261003.json),
[subparsers run 2](data/callmethod-outline-subparsers-r2-20261003.json), and
[`function_calls`](data/callmethod-outline-function-calls-20261003.json).
The fixed `build-repro/Release` binaries were not used or modified.
