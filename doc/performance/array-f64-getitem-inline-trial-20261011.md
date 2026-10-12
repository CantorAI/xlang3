# Rejected direct `array('d')` getitem trial — 2026-10-11

## Question

SciMark performs many numeric array reads. Could the cached VM `GetItem`
instruction read exact `array('d')` + exact `Int64` indexes directly, avoiding
the native function call wrapper on unobserved execution?

## Result

The trial was rejected. Compared with the saved same-day fixed-Release XLang3
control, pyperf reported `scimark_fft` 1.06x faster and
`scimark_monte_carlo` 1.02x faster, while hiding the other three cases as
statistically insignificant. Its geometric mean was **1.00x slower**. The
candidate samples were noisy: FFT was 935 ms ± 54 ms, LU 1.05 s ± 0.15 s,
Monte Carlo 537 ms ± 8 ms, SOR 884 ms ± 110 ms, and sparse matrix multiply
17.5 ms ± 0.4 ms. These measurements do not establish a useful overall gain.

The candidate passed all 55 CTest checks, but that correctness result does not
change the performance decision. The VM intrinsic, opt-in field, and helper
were removed; the generic native `__getitem__` path remains authoritative.

## Reproduction and evidence

The benchmark used Python 3.14.7 and the fixed Release directory
`build-repro/main-verify-20261006/Release`. The command was:

```powershell
& 'C:\Python\Python314\python.exe' benchmarks\diagnostics\run_pyperformance_xlang3_shimmed.py `
  --runtime build-repro\main-verify-20261006\Release\xlang3.exe `
  --benchmarks scimark --mode fast --case-timeout 300 `
  --output doc\performance\data\scimark-array-f64-getitem-inline-candidate-20261011.pyperf.json `
  --dependency-site 'C:\Python\Python314\Lib\site-packages'
```

Comparison:

```powershell
& 'C:\Python\Python314\python.exe' -m pyperf compare_to `
  doc\performance\data\scimark-current-fixed-release-full-fast-20261011.pyperf.json `
  doc\performance\data\scimark-array-f64-getitem-inline-candidate-20261011.pyperf.json --table
```

The candidate executable SHA-256 was
`D84F938C82DC2A69FC01ED4A6B808B16F4D33267E7093B702DF0950EC8F13C1F`; its
runtime DLL SHA-256 was
`775FFF0455CCA35A2276EB160F170D1FD746E7F3DE7BCF69237D50EE31353252`.
The saved [control data](data/scimark-current-fixed-release-full-fast-20261011.pyperf.json)
and its [provenance](data/scimark-current-fixed-release-full-fast-20261011.pyperf.provenance.json),
along with the candidate [raw pyperf data](data/scimark-array-f64-getitem-inline-candidate-20261011.pyperf.json),
are retained for audit.
