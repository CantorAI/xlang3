# Monomorphic Python-function dispatch cache for `CallEx` — 2026-10-04

## Change and guard

`CallEx` now keeps a per-instruction Python-function target after the first
expanded call. On later executions it compares the live callee's object
identity with the cached target and calls `call_user_function` directly when
they match. This avoids repeating the bound-method/function/native/class type
dispatch chain at a stable Python-function call site. Argument expansion,
signature binding, defaults, monitoring, generator handling, recursion checks,
frame creation, and tracebacks remain on the existing path.

The async-tree workload has a monomorphic `asyncio.gather(*children)` site.
The cache retains the target for pointer safety, but checks the current callee
on every hit, so replacing the target at the same call site falls back to
normal dispatch and warms the cache for the replacement. The compatibility
fixture alternates two functions and changes a live function default after the
first call. No Python standard-library implementation was replaced.

## Measurement

This comparison used two official pyperformance 1.14.0 `async_tree_eager`
fast-mode runs per build. The parent executable and runtime DLL were copied
before rebuilding, so the pair differs only by this `CallEx` cache; both
include the preceding keyword-binder change. The runs were executed in
candidate/control, then control/candidate order.

| Build | Run 1 | Run 2 | Combined pyperf comparison |
|---|---:|---:|---:|
| Parent control | 2.84 s ± 0.04 s | 2.88 s ± 0.04 s | 2.86 s |
| Candidate | 2.79 s ± 0.03 s | 2.85 s ± 0.04 s | 2.82 s; pyperf reports 1.01× faster |

Fast-mode workers warned that sample counts were too small for a stable result.
The merged comparison is a modest directional gain, not evidence that the
async-tree gap is closed. A separate rigorous-mode control attempt exceeded
the 300-second full-case limit before producing a benchmark result; it is not
included in the comparison.

Raw fast-mode results:

- Control run 1: [`JSON`](data/async-tree-callex-function-cache-control-fast-20261004.json)
- Control run 2: [`JSON`](data/async-tree-callex-function-cache-control-fast-r2-20261004.json)
- Merged control: [`JSON`](data/async-tree-callex-function-cache-control-merged-fast-20261004.json)
- Candidate run 1: [`JSON`](data/async-tree-callex-function-cache-candidate-fast-20261004.json)
- Candidate run 2: [`JSON`](data/async-tree-callex-function-cache-candidate-fast-r2-20261004.json)
- Merged candidate: [`JSON`](data/async-tree-callex-function-cache-candidate-merged-fast-20261004.json)

## Validation

- The target-rebinding and live-default fixture matched CPython 3.14.7.
- `xlang3_interpreter_tests` passed on the rebuilt Release candidate.
- All 11 fixed Release regression cases passed; the largest candidate/baseline
  ratio was `range_for` at 1.010×, under the 1.10 limit. The paired results are
  in [`the gate report`](data/fixed-release-gate-callex-function-cache-20261004.json).
- The full fixture runner again stopped at the existing `ctypes_pointer_return`
  environment failure: this build reports `libffi is unavailable for ctypes`.

The exact candidate completed a fresh all-97 pyperformance run against the
saved CPython 3.14.7 baseline. It completed 45 definitions and recorded 52
failures or timeouts, with 49 matched subtests and five measured wins. The
CPython/XLang3 geometric mean is 0.16458× (ratios above 1× favor XLang3), so
this cache has not materially changed the overall result. The refreshed
[comparison report](../pyperformance-xlang3-callex-cache-vs-cpython314-fast-20261004.md)
includes the horizontal ratio chart, per-case status, and run hashes. The full
speed goal remains open; this cache is a small local improvement against much
larger remaining slowdowns.
