# Guarded integer `__getitem__` inline trial — 2026-10-11

## Result

The candidate completed all five official pyperformance SciMark subtests.
Against the saved same-day XLang3 fixed-Release control, `scimark_lu` improved
from 1.01 s to 298 ms (**3.37× faster**). The five-case geometric mean is
**1.28× faster**. Pyperf found only LU significant; FFT, Monte Carlo, SOR, and
sparse matrix multiply were statistically hidden.

| Official pyperformance case | Saved XLang3 control | Candidate | Candidate / control |
|---|---:|---:|---:|
| `scimark_fft` | 991 ms | 979 ms | Not significant |
| `scimark_lu` | 1.01 s | 298 ms | **3.37× faster** |
| `scimark_monte_carlo` | 551 ms | 547 ms | Not significant |
| `scimark_sor` | 843 ms | 843 ms | Not significant |
| `scimark_sparse_mat_mult` | 17.5 ms | 17.5 ms | Not significant |
| **Geometric mean** |  |  | **1.28× faster** |

Against the saved CPython 3.14.7 SciMark run, candidate times are 4.22× slower
on FFT, 3.42× slower on LU, 8.99× slower on Monte Carlo, 6.12× slower on SOR,
and 2.99× slower on sparse matrix multiply. The five-case geometric mean is
**4.73× slower than CPython**. This targeted change materially improves LU but
does not make SciMark or the full suite faster than CPython.

Fast-mode pyperf warned that several cases might be unstable. Treat the
reported magnitude as directional, and retain the raw data and provenance for
reanalysis. No full-suite score is claimed from this group run.

## Implementation and safety

The VM recognizes a narrow IR pattern in a Python `__getitem__` method:
the non-tuple branch `return self.data[index]`. It bypasses the Python method
frame only for an exact nonnegative integer, a canonical live `isinstance` and
`tuple`, an exact eligible receiver class with no attribute hooks/descriptors,
an exact list backing store, an in-range index, and an unobserved execution
state. Any failed guard returns to the normal Python frame before executing
the method body. The implementation is a runtime dispatch optimization whose
eligibility proof comes from function IR; it does not rewrite IR.

The first candidate crashed because the analyzer made a temporary `Value`
from a borrowed class pointer without marking it borrowed. Its destructor
decremented the class reference count, and LU crashed on a subsequent access.
The analyzer now marks the temporary with
`kXlangValueBorrowedRefFlag`; an object-valued repeated-index fixture guards
that lifetime. The corrected fixed-Release build completes LU at N=30, the
focused fixture matches CPython 3.14.7, and 54/54 CTest checks pass.

## Reproduction

The run used Python 3.14.7, pyperformance 1.14.0, fast mode, the fixed Release
directory `build-repro/main-verify-20261006/Release`, and the shared 3.14
benchmark dependency site. The command was:

```powershell
& 'C:\Python\Python314\python.exe' benchmarks\diagnostics\run_pyperformance_xlang3_shimmed.py `
  --runtime build-repro\main-verify-20261006\Release\xlang3.exe `
  --benchmarks scimark --mode fast --case-timeout 300 `
  --output doc\performance\data\scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json `
  --dependency-site 'C:\Python\Python314\Lib\site-packages'
```

Compare with the saved fixed-Release control and CPython 3.14.7 data:

```powershell
& 'C:\Python\Python314\python.exe' -m pyperf compare_to `
  doc\performance\data\scimark-current-fixed-release-full-fast-20261011.pyperf.json `
  doc\performance\data\scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json --table

& 'C:\Python\Python314\python.exe' -m pyperf compare_to `
  doc\performance\data\scimark-cpython3147-status-20261011.pyperf.json `
  doc\performance\data\scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json --table
```

Raw candidate data: [`scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json`](data/scimark-int-attr-getitem-candidate-20261011-r2.pyperf.json).
Hashes, exact worker summaries, and the command are in its
[provenance](data/scimark-int-attr-getitem-candidate-20261011-r2.pyperf.provenance.json).
