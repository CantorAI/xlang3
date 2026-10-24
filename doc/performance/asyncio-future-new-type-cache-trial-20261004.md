# `_asyncio.Future.__new__` native type cache trial — 2026-10-04

## Result

The change fixes a Python 3.14 compatibility bug and removes a repeated `_asyncio.Future` module lookup from native Future construction. The focused `async_tree_eager` measurement moved from 2.97 s to 2.94 s (about 1.01x faster), but pyperf marked the run unstable; treat this as a small directional result, not a confirmed speedup. The benchmark is still about 33.9x slower than CPython 3.14.7's 86.62 ms result.

The required fixed-baseline regression gate passed 10 of 11 cases in both runs. `subparsers` repeatedly measured around 1.08–1.09x the preserved baseline. The gate returned inconclusive because its first sample's 95% interval crossed the 1.10 regression threshold, then its automatic confirmation passed. A second complete run showed the same pattern. This benchmark is outside the changed asyncio path, and prior gate evidence also put it near the threshold; do not attribute this result to the Future change.

## Change and compatibility coverage

CPython's native Future constructor checks the actual native Future type, rather than looking up the current mutable `_asyncio.Future` module attribute. XLang3 previously imported `_asyncio` and read that attribute on each construction. A saved native class therefore failed to construct after its module attribute was rebound, unlike CPython.

The native `__new__` callback now receives the per-runtime `Future` class identity through its callback context. It checks the requested type against that cached identity and avoids an import and attribute lookup on every construction. The source comment at this code records both the CPython behavior and the hot-path reason for the cache. The new `asyncio_future_new_module_rebind` fixture verifies that the saved native type still constructs after the module attribute changes.

Validation on Python 3.14.7: the full XLang3 fixture sweep and `xlang3_interpreter_tests` passed. The focused fixture prints `saved-native-type True` on both CPython and the candidate.

## Benchmark evidence

The official `async_tree_eager` pyperformance command used CPython 3.14.7 and the same Python 3.14 dependency site for both XLang3 samples. Pyperf reported:

| Runtime | Median | Relative to prior XLang3 sample |
| --- | ---: | ---: |
| Before type cache | 2.97 s | 1.00x |
| With type cache | 2.94 s | 1.01x faster (unstable) |
| CPython 3.14.7 | 86.62 ms | about 33.9x faster than XLang3 candidate |

Raw pyperf data:

- Control: `data/async-tree-native-entry-fast-control-20261004.json`
- Candidate: `data/async-tree-future-new-type-cache-candidate-20261004.json`

The exact comparison can be reproduced with:

```powershell
C:\Python\Python314\python.exe -m pyperf compare_to --table `
  doc\performance\data\async-tree-native-entry-fast-control-20261004.json `
  doc\performance\data\async-tree-future-new-type-cache-candidate-20261004.json
```

The two full Release-gate reports are `data/release-regression-async-future-type-cache-20261004.json` and `data/release-regression-async-future-type-cache-rerun-20261004.json`. Both used the preserved `build-repro/Release/xlang3.exe` baseline and the scratch Release candidate; both recorded the `subparsers` confirmation pattern described above.

Candidate executable SHA-256: `1ac96e9d3907f4ccf938146c8c3a40e5df1efe65489ac47556f884119fb57c75`.

This is a correctness and dispatch-overhead improvement, not a material closure of the pyperformance gap. The next optimization should target a measured hot path responsible for a large share of `async_tree_eager` time, then rerun this benchmark and the full fixed-baseline gate.
