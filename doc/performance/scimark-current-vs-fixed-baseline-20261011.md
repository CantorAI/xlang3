# SciMark current Release versus fixed baseline (2026-10-11)

## Result

The current Release candidate is directionally faster than the preserved fixed
baseline on four of five SciMark pyperformance subtests. `pyperf compare_to`
reports a 1.04x geometric-mean gain over the baseline and hides
`scimark_sparse_mat_mult` as not statistically significant. This is a modest
improvement, not the requested large speed gain. The benchmark output contains
unstable-result warnings, and the current source includes many changes since
the baseline build, so the aggregate cannot be attributed to a particular
optimization.

All five cases remain slower than CPython 3.14.7. The largest gap in this set
is now `scimark_lu` at 11.67x slower; the baseline was 13.57x slower. That is a
1.16x candidate-over-baseline improvement, consistent with the separate
diagnostic showing a large XLang3 cost for Python `ArrayList.__getitem__`.
The diagnostic narrows a hot path but does not prove that it caused this
official-score change.

| pyperformance case | CPython 3.14.7 | Fixed baseline | Current candidate | Candidate vs baseline | Candidate vs CPython |
|---|---:|---:|---:|---:|---:|
| `scimark_fft` | 232 ms | 1.17 s | 972 ms | 1.20x faster | 4.19x slower |
| `scimark_lu` | 87.1 ms | 1.18 s | 1.02 s | 1.16x faster | 11.67x slower |
| `scimark_monte_carlo` | 60.9 ms | 576 ms | 555 ms | 1.04x faster | 9.12x slower |
| `scimark_sor` | 138 ms | 926 ms | 863 ms | 1.07x faster | 6.26x slower |
| `scimark_sparse_mat_mult` | 5.86 ms | 18.6 ms | 23.4 ms | 1.26x slower (not significant) | 4.00x slower |

Each run used pyperformance 1.14.0 in fast mode with Python 3.14.7 at
`C:\Python\Python314`, the same explicit compatibility/dependency site, and
the same `scimark` benchmark group. They were run sequentially as candidate,
CPython, then fixed baseline; this is not a randomized paired schedule. Several
subtests emitted pyperf instability warnings, so use these results to select
the next investigation, not as final published scores.

## Reproduction data

- [CPython 3.14.7 raw data](data/scimark-cpython3147-status-20261011.pyperf.json)
- [Fixed baseline raw data](data/scimark-fixed-baseline-status-20261011.pyperf.json)
- [Current candidate raw data](data/scimark-current-candidate-status-20261011.pyperf.json)
- [Baseline-to-candidate compare output](data/scimark-fixed-baseline-vs-current-status-20261011.txt)

The current candidate is `build-repro/main-verify-20261006/Release/xlang3.exe`
(SHA-256 `F08973F66FE5518FC2C4B2237F3265CC9CE2D381CAF72457179C4E03D6A9FE5B`)
with runtime DLL SHA-256
`11178F776512621E5C1D02131401CE567FF4822D8F54FEF00F81FBE59EC61676`. The
fixed baseline is
`build-repro/baseline-postfix-candidate-full-fast-20261011/Release/xlang3.exe`
(SHA-256 `BE0E00D5875D5887B1C7B9345066B82E9DDA450C75FD62AFF8614823A1B7F97B`)
with runtime DLL SHA-256
`E4C8ACE7413DC3782AC62513733EE664D3394BD942E666A126E9201711758249`.

## Optimization direction

`scimark_lu` repeatedly invokes Python `ArrayList.__getitem__`. A separate
same-source diagnostic measured 50,000 `a[i]` calls at 61.9 ms on the fixed
XLang3 Release executable versus 5.01 ms on CPython; direct `a.data[i]` access
was 25.2 ms versus 3.34 ms. These counts isolate a real custom-method/frame
cost, on top of general VM overhead. Continue by measuring and specializing
the generic Python method-call/frame path using live IR and mutation/observer
guards. Do not replace the pure-Python SciMark helper or its `__getitem__`
implementation with C++.
