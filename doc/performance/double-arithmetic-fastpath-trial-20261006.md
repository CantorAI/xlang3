# Exact-double arithmetic fast-path trial — 2026-10-06

## Result

Rejected and reverted. The trial added direct `Double`/`Double` cases to the
shared VM's add, subtract, multiply, and divide helpers. It was motivated by
CPython 3.14.7's warmed raytrace bytecode specializing exact-float arithmetic.
The candidate preserved all existing mixed-number and generic fallback paths.

The unchanged official `pyperformance` raytrace benchmark did not establish a
gain:

| Measurement | Control | Candidate | Result |
| --- | ---: | ---: | --- |
| Official fast mode | 2.19 s | 2.39 s ± 0.06 s | Candidate/control 1.10x slower; pyperf warned the result was unstable |
| Order-balanced direct official body, 7 pairs | 2,354.7 ms median | 2,318.7 ms median | Candidate/control 0.9869x; 95% interval 0.9815–1.0080, includes parity |

The official rigorous-mode run hit its 300-second case timeout before producing
results. That run is inconclusive. The local paired check invokes the unchanged
official `bench_raytrace(1, 100, 100, None)` body but is only a targeted paired
measurement; it does not replace pyperf. Neither measurement supports keeping
the code change. The full 11-case Release gate was not run for a rejected,
unvalidated candidate.

## Correctness and rollback

The candidate build passed the full Python fixture runner under CPython 3.14.7
and the Release `xlang3_interpreter_tests.exe` and
`xlang3_runtime_value_tests.exe` binaries. Those checks did not establish a
performance gain. The implementation was reverted and the fixed Release
runtime restored byte-for-byte to the preserved control.

- Candidate runtime DLL SHA-256: `219EDF32B5E72EA02B50076C6E48E559BC3C918E823C002F05162F05F48A7E83`.
- Restored fixed executable SHA-256: `7FB9FD272F16D0374596DA1149F61226F3011BB2EC80539F70900CDBB68B357D`.
- Restored fixed runtime DLL SHA-256: `CCC5EC8DDA34F0686799FCBEF0080907B30F27D79F8E44D0C28B9A9EE4C33259`.

## Evidence

- [Official fast candidate](data/raytrace-double-fast-candidate-fast-20261006.json)
- [Official rigorous timeout log](data/raytrace-double-fast-candidate-rigorous-timeout-20261006.txt)
- [Order-balanced paired samples](data/raytrace-double-fast-paired-20261006.json)
- [Paired workload source](data/raytrace-pair-20261006.py)

Do not repeat this exact direct-double branch. The VM already has generic
numeric fast helpers; simply bypassing their repeated numeric-tag conversion
did not produce a reliable end-to-end raytrace win.
