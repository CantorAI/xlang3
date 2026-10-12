# Exact-arity cached Python method entry — 2026-10-04

## Change

`CallMethod` already caches a direct Python method behind the receiver class's
version. On a cold lookup, this trial checks whether the function has a fixed
positional signature that exactly matches the receiver plus explicit arguments.
The cache stores that arity. A later hit with the same class version and the
same argument shape enters the ordinary Python frame directly, avoiding the
generic argument binder's repeated signature scan.

The optimization leaves frame creation, tracing, profiling, recursion checks,
and traceback behavior on the regular frame path. It rejects keyword and
expanded calls, generators, arity mismatches, class changes, and instance
attribute shadowing; those cases keep their existing generic dispatch. The
guard rationale is documented next to the helper in
`src/executor/xlang_vm/ops/xlang_vm_ops_call.h`. The regression fixture covers
normal calls, class replacement, and instance shadowing.

This changes XLang3 VM dispatch only. It does not replace or modify a
CPython pure-Python library.

## Measurements

The unchanged subparsers workload was measured in 21 order-balanced pairs
against the immediately preceding XLang3 Release build:

| Workload | Previous XLang3 | Candidate | Candidate / previous |
|---|---:|---:|---:|
| Local pyperformance-shaped `subparsers` | 232.803 ms | 227.853 ms | 0.9819× (95% CI 0.9772–0.9838) |
| Official pyperformance 1.14 `argparse_subparsers`, rigorous | 143 ms ± 1 ms | 141 ms ± 1 ms | about 1.014× faster |

The candidate also measured 2.82 s on `async_tree_eager` versus 2.79 s before,
and 5.10 ms on `pickle_pure_python` versus 5.13 ms before. These fast-mode
results show no meaningful change in those cases. This is a small targeted win,
not a resolution of the overall performance gap: the saved CPython 3.14.7
`subparsers` result is 8.151 ms, so the candidate remains about 17.4× slower.

## Validation

- Python 3.14.7 and pyperformance 1.14.0 only.
- The new exact-positional cache fixture passed, including method replacement
  and instance-shadow behavior.
- The complete core and compatibility fixture runner passed after building and
  staging the full candidate native-module set.
- `xlang3_interpreter_tests.exe` passed.
- The complete 11-case fixed Release regression gate passed. `subparsers` was
  1.067× the fixed Release score, within the repository's 1.10 limit.
- Fixed Release executable and runtime DLL hashes were not modified.

## Evidence

- Order-balanced result: `data/callmethod-exact-cache-subparsers-order-balanced-20261004.json`.
- Official candidate: `data/pyperformance-xlang3-callmethod-exact-cache-subparsers-candidate-rigorous-20261004.json`.
- Official previous build: `data/pyperformance-xlang3-callmethod-exact-cache-subparsers-control-rigorous-20261004.json`.
- Official fast-mode screening: `data/pyperformance-xlang3-callmethod-exact-cache-fast-20261004.json`.
- Fixed Release gate: `data/release-regression-callmethod-exact-cache-20261004.json`.

The full CPython 3.14 comparison remains in
`pyperformance-xlang3-all-97-current-candidate-vs-cpython314-fast-20261004.md`.
