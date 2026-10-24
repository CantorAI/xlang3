# SciMark LU: `ArrayList.__getitem__` call-frame cost (2026-10-11)

## Finding

The official pyperformance SciMark LU workload repeatedly indexes a Python
`ArrayList` whose `__getitem__` branches on `isinstance(idx, tuple)` and, for
integer indices, returns `self.data[idx]`. XLang3 caches lookup of this special
method and avoids allocating a bound-method object, but still pushes a Python
frame for each `__getitem__` invocation. This is a real remaining cost in a
hot path, separate from native list indexing and the general VM-operation gap.

The benchmark implementation is installed with pyperformance 1.14.0 at
`C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_scimark\run_benchmark.py`.
`LU_factor` repeatedly evaluates `A[i][j]`; `ArrayList.__getitem__` uses the
integer branch for the row read, after which the returned `array('d')` is
indexed. The VM's current custom-instance `GetItem` path is in
`src/executor/xlang_vm/ops/xlang_vm_ops_containers.h` near the
`GetItemUserFunction` dispatch.

## Isolated diagnostic

A temporary same-source microbenchmark ran 50,000 calls per sample, warmed up
twice, and took the median of seven samples on each interpreter. It compared
`a[i]`, which runs the Python `__getitem__`, against `a.data[i]`, which skips
that method. The temporary script was removed after the run.

| Operation | CPython 3.14.7 | XLang3 fixed Release | XLang3 / CPython time |
| --- | ---: | ---: | ---: |
| `a[i]` through Python `__getitem__` | 5.007 ms | 61.894 ms | 12.36× |
| `a.data[i]` direct list access | 3.342 ms | 25.239 ms | 7.55× |

The custom method costs about 2.45× the direct-list loop in XLang3, compared
with 1.50× in CPython. That is evidence that Python method-frame dispatch is a
distinct opportunity on top of the broad VM overhead. This microbenchmark is
diagnostic only: it is not a pyperf result and does not quantify how much of
the official LU gap this one method accounts for.

## Optimization constraints

A promising next experiment is a guarded inline for the integer branch of a
recognized `__getitem__` body. It must be derived from function IR and fall
back to the ordinary frame before executing any part of the method when the
proof fails. In particular, the proof must account for code replacement,
module-global or builtin rebinding of `isinstance`/`tuple`, class mutation,
custom attribute hooks, tracing/profiling/monitoring, and index types outside
the exact integer case. The inlined path must retain normal `self.data`
attribute lookup and list-index errors. A class-name check or a special case
for the pyperformance benchmark would not be acceptable.

## Measured guarded inline trial

The first inline candidate crashed in LU because its IR analyzer built a
temporary `Value` from a borrowed `ClassObject*` without setting the borrowed
reference flag. Destroying that temporary incorrectly decremented the class
reference count. LU at N=2 reproduced the access violation; the same probe
passed when the fast path was bypassed. Marking the temporary borrowed fixed
the failure. The regression fixture now repeatedly reads object-valued rows
through one `GetItem` call site to protect this ownership rule.

With the analyzer eligibility cached at the `GetItem` call site, the complete
official pyperformance SciMark group completed. `scimark_lu` improved from
1.01 s to 298 ms, **3.37x faster** than the saved fixed-Release XLang3
control. The geometric mean across all five SciMark cases was **1.28x faster**;
the four unchanged cases were statistically hidden by pyperf. Against the
saved CPython 3.14.7 run, SciMark LU remains **3.42x slower**, and the five-case
geometric mean remains **4.73x slower**. The optimization is a material local
gain, not a claim that SciMark or the full suite now beats CPython.

The candidate passed the focused fixture, the direct LU reproduction, and all
54 CTest checks (excluding the separate CLI-fixture aggregate). Fast-mode
pyperf marked several raw cases unstable; only LU was significant in the
XLang3 control comparison. Raw results, provenance, and the comparison are in
[`scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json`](data/scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json),
its provenance file, and the adjacent trial report. This remains an
uncommitted working-tree candidate pending a broader full-suite validation.

## Current-source isolated A/B — 2026-10-11

To verify the effect on the current fixed Release build, I disabled only the
call to `try_cached_int_attr_getitem` for a control build, then restored it and
rebuilt the candidate. The ordinary `GetItemUserFunction` cache and the
`isinstance` shortcut remained enabled in both arms. Both runs used the same
CPython 3.14.7 pyperformance 1.14.0 fast-mode runner and fixed Release output
directory. The comparison is source-matched except for this one guard.

| SciMark subtest | Inline disabled | Inline enabled | Enabled vs disabled |
|---|---:|---:|---:|
| `scimark_lu` | 771 ms ± 24 ms | 306 ms ± 4 ms | 2.52x faster |
| `scimark_fft` | 1.05 s ± 0.10 s | 996 ms ± 10 ms | 1.06x faster |
| `scimark_monte_carlo` | 534 ms ± 18 ms | 538 ms ± 7 ms | not significant |
| `scimark_sor` | 874 ms ± 20 ms | 870 ms ± 7 ms | not significant |
| `scimark_sparse_mat_mult` | 17.4 ms ± 0.3 ms | 18.3 ms ± 0.3 ms | 1.05x slower |

`pyperf compare_to` reports a 1.20x geometric-mean gain over the five cases;
the LU improvement is significant, while two cases are hidden as insignificant
and sparse multiply regresses slightly. An independent enabled run before the
control measured LU at 348 ms ± 18 ms, also below the 771 ms control. This
confirms the frame-elision path materially reduces the official LU time, but
does not show XLang3 beating CPython: the enabled result remains 306 ms vs
75.3 ms, or 4.06x slower than CPython 3.14.7.

The same-source disabled executable SHA-256 is
`BC67438601C2C89111B1F3D9384AD2896CE7EA2C3C500ED19DB48013849DE6DA` (DLL
`A820C35A7AB90432C9A6CEFF520694D89E4D265B25401F96F1E883B6DF2BB9EF`). The
enabled executable SHA-256 is
`63A213AF31E259357F12D3801D3CAF300ACF756EE80F5DBAA552D3B5C00AB255` (DLL
`CA0E993B4E0F48F3F22C23E7279C9B6F1076313410080374631894BC8DF56DD3`). Evidence:

- [Same-source disabled-control pyperf JSON](data/scimark-getitem-inline-isolated-control-20261011.pyperf.json)
- [Same-source enabled-candidate pyperf JSON](data/scimark-getitem-inline-isolated-candidate-r2-20261011.pyperf.json)
- [Earlier enabled run on the restored fixed build](data/scimark-current-fixed-release-restored-20261011.pyperf.json)
- [CPython 3.14.7 reference JSON](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json)
