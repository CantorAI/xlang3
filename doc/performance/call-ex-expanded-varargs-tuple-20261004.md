# Rejected direct tuple construction for expanded varargs — 2026-10-04

**Status: rejected and removed.** This historical trial measured a candidate
that directly built the `*args` tuple from exact tuple/list expansion. A later
source-matched rerun reversed the apparent small whole-workload gain, so the
non-empty tuple/list fast path is not present in the current binder. The
retained current-call-expansion result is documented in
[`call-exact-varargs-star-fastpath-trial-20261004.md`](call-exact-varargs-star-fastpath-trial-20261004.md).

## Change and reason

The eager async-tree benchmark calls the Python `asyncio.gather` function with
`*` applied to a list comprehension. Its signature is
`(*coros_or_futures, return_exceptions=False)`, so the earlier experiment for
functions whose complete signature was exactly `*args` did not cover this hot
call shape. The native VM profile attributed 36.17% of positive measured
self-time to `CallEx`; that instrumented profile locates the cost but is not an
ordinary-build timing.

For calls containing no explicit positional or keyword arguments, with only a
starred positional expansion and a signature consisting of `*args` followed
by defaulted keyword-only parameters, the binder now skips its general
positional-parameter scan. Exact tuple/list operands are copied straight into
the final tuple's reserved storage, avoiding both temporary positional vectors.
Arbitrary iterables still use the iterator protocol and preserve their
side-effects. Every other call shape continues through the general binder.
Comments beside the code explain the allocation avoided and why dynamic
iterables stay on the protocol path. No standard-library Python source was
replaced.

## Measurements

| Workload | Control | Candidate | Candidate / control |
|---|---:|---:|---:|
| 200,000 repeated starred calls to a varargs-plus-defaulted-keyword-only function | 164.9 ms | 83.5 ms | 0.509× (95% interval 0.504–0.512) |
| Official `async_tree_eager` body, 21 order-balanced pairs | 2,820.0 ms | 2,808.7 ms | 0.9972× (95% interval 0.9940–0.9990) |
| Official pyperformance 1.14.0 `async_tree_eager`, fast mode | 2.82 s ± 0.03 s | 2.80 s ± 0.02 s | 1.01× faster as rounded by `pyperf compare_to` |

The isolated binding loop is nearly 2× faster, while the full async-tree
improvement is small (about 0.3% in the balanced source-matched run). This is
useful targeted progress, not a large overall speedup. The saved CPython
3.14.7 result is 86.6 ms, leaving the candidate about 32.34× slower.

## Validation and evidence

- Python 3.14.7 produced the fixture's expected output; the full Python
  fixture suite and `xlang3_interpreter_tests` passed on the candidate.
- The candidate passed all 11 fixed-Release regression cases. The slowest was
  `subparsers` at 1.077× of baseline, under the 1.10 limit.
- The preserved fixed Release executable remains SHA-256
  `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.
- Candidate executable SHA-256:
  `57511DD72B472DB10C9A9D2ED3E5AE6F445F3433A04C045416BCCDC8E7567429`.
- Candidate runtime DLL SHA-256:
  `9CC376BD56DF413A02122485352BEBD33EDC90B7F5FD860B3EB67BB125B630FA`.
- Balanced eager-tree raw data:
  [`async-tree-varargs-direct-tuple-candidate-rigorous-20261004.json`](data/async-tree-varargs-direct-tuple-candidate-rigorous-20261004.json).
- Focused binder raw data:
  [`call-ex-starred-varargs-micro-candidate-fast-20261004.json`](data/call-ex-starred-varargs-micro-candidate-fast-20261004.json).
- Official pyperformance candidate and fixed-Release results:
  [`candidate`](data/pyperformance-xlang3-async-tree-varargs-direct-20261004.json),
  [`fixed Release`](data/pyperformance-xlang3-async-tree-fixed-release-20261004.json).
- Fixed-Release 11-case gate:
  [`release-regression-call-ex-direct-bind-20261004.json`](data/release-regression-call-ex-direct-bind-20261004.json).

The overall full-suite speed objective remains open. The other largest gaps
in the latest complete Python 3.14 comparison were `telco`, pure-Python
pickle, and `subparsers`; this change does not address those workloads.
