# `argparse_subparsers`: registry-get inlining trial (2026-10-06)

## Result

A guarded IR-pattern optimization was prototyped for a frequently called
four-argument Python method with the body shape
`return self.mapping[registry].get(key, default_value)`. The motivation was
CPython 3.14.7 argparse call-profile evidence: `_registry_get` runs about
3,500 times in the official `subparsers` workload, while VM dispatch accounts
for most of XLang3's measured runtime. The prototype kept Python attribute and
mapping behavior in XLang3 and fell back for unsupported key types or active
monitoring/profile hooks.

The official pyperformance 1.14 `bm_argparse/run_benchmark.py subparsers`
workload did not improve. The fixed Release control median was **145 ms**
(149 ± 18 ms mean); the candidate median was **146 ms** (148 ± 11 ms mean).
`pyperf compare_to` hid the candidate because the difference was not
statistically significant. Both runs reported host jitter and large maximum
outliers, so the 1 ms median difference is not evidence of a regression or a
gain. The prototype was removed from source.

CPython 3.14.7, run through the same pyperf 1.14 benchmark script, measured
**7.83 ms median** (7.99 ± 0.74 ms mean). The XLang3 candidate therefore
remains about **18.6× slower** by medians. Pyperf warned about a 12.6 ms
maximum outlier in the CPython run; the previously preserved full-suite
CPython result for this benchmark is 8.151 ms, consistent with this reference.

## Reproduction

The benchmark was invoked directly through the exact pyperformance 1.14
`bm_argparse/run_benchmark.py` script with pyperf's `--rigorous` option. This
uses the benchmark's original `bm_subparsers` body. Direct invocation avoids
pyperformance creating a new XLang3 venv: that setup currently fails during
`ensurepip` because the XLang3 runtime does not provide Python's `mmap` module.
The same `pyperf_compat` environment hook and CPython 3.14.7 pyperformance
site-packages were available to both runtimes.

| Runtime | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Fixed XLang3 Release control | `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548` | `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D` |
| Candidate XLang3 Release | `CB77389B5C3962C8B933698D2309DD75699CA5FFABA69778C7E14621D6D9CB35` | `EC785BBF5F89312F58C7248C8A7A263B5E1A4E2F352A746D51B39F3F7ADA1B3B` |
| CPython 3.14.7 | `C:\Python\Python314\python.exe` | — |

The candidate was built from commit `d57a94469a67274342f85a8b9beff473999249fe`
with MSVC Release `/O2 /Ob3`, using `Ninja Multi-Config` and the repository's
Python 3.14.7 configuration. The fixed control artifacts live under
`scratch/performance/register-buffer-async-tree-trial-20261006/control`.

## Raw data

- [XLang3 candidate](data/pyperformance-subparsers-inline-registry-get-candidate-xlang3-20261006.json)
- [Fixed XLang3 control](data/pyperformance-subparsers-inline-registry-get-fixed-control-xlang3-20261006.json)
- [CPython 3.14.7 reference](data/pyperformance-subparsers-inline-registry-get-cpython314-20261006.json)

This trial does not change the full-suite score or the existing performance
chart. The call-handler profile remains a useful target, but this individual
method-body inline did not reduce the measured benchmark time.

