# Current Release versus fixed baseline: async tree control

## Result

The current fixed-path Release candidate does not show an end-to-end gain on
the non-I/O `async_tree` control. Using pyperformance 1.14.0 in fast mode and
the same harness/Python 3.14.7 installation, XLang3 measured 4.00 s on the
current candidate, 3.86 s on the preserved fixed baseline, and 241 ms on
CPython 3.14.7. `pyperf compare_to` reports the baseline at 16.02× and the
candidate at 16.61× slower than CPython; it reports the candidate 1.04× slower
than the baseline. All three runs emitted pyperf's unstable-result warning,
so these numbers are screening evidence, not a statistically settled
regression. They provide no evidence of a broad speedup.

This control does not schedule the large I/O timer workload. The poor ratio
therefore cannot be attributed to `TimerHandle.__lt__` or native `_heapq`
alone. The comparison-cache/inliner trial may reduce one call path, but the
async task and VM execution cost remains dominant on this control.

## Reproduction

All runs used `C:\Python\Python314\python.exe` (Python 3.14.7), the fixed
Release paths, the same explicit pyperf compatibility/dependency site, and
`run_pyperformance_xlang3_shimmed.py --benchmarks async_tree --mode fast`.
Only `--runtime` and `--output` differed. The saved executables were:

- Current candidate: `build-repro/main-verify-20261006/Release/xlang3.exe`,
  SHA-256 `F08973F66FE5518FC2C4B2237F3265CC9CE2D381CAF72457179C4E03D6A9FE5B`.
  Runtime DLL SHA-256:
  `11178F776512621E5C1D02131401CE567FF4822D8F54FEF00F81FBE59EC61676`.
- Fixed baseline: `build-repro/baseline-postfix-candidate-full-fast-20261011/Release/xlang3.exe`,
  SHA-256 `BE0E00D5875D5887B1C7B9345066B82E9DDA450C75FD62AFF8614823A1B7F97B`.
  Runtime DLL SHA-256:
  `E4C8ACE7413DC3782AC62513733EE664D3394BD942E666A126E9201711758249`.

Raw pyperf JSON:

- [CPython 3.14.7](data/async-tree-cpython3147-status-20261011.pyperf.json)
- [Fixed Release baseline](data/async-tree-fixed-baseline-status-20261011.pyperf.json)
- [Current Release candidate](data/async-tree-current-candidate-status-20261011.pyperf.json)

The exact table can be regenerated with:

```powershell
& 'C:\Python\Python314\python.exe' -m pyperf compare_to --table `
  doc\performance\data\async-tree-cpython3147-status-20261011.pyperf.json `
  doc\performance\data\async-tree-fixed-baseline-status-20261011.pyperf.json `
  doc\performance\data\async-tree-current-candidate-status-20261011.pyperf.json
```

## Next diagnostic direction

The separate official-workload counters show similar Python call counts on
XLang3 and CPython, while XLang3 executes millions of local-attribute, local,
container-index, and call operations. That points toward higher cost per VM
operation or object access, rather than an excess number of Python calls.
Measure an unchanged-source VM hot-loop control and attribute/call shapes
against both fixed XLang3 baseline and CPython before attempting another broad
dispatch rewrite. Keep `async_tree_io` timer callback costs as a separate
optimization target; it cannot explain this non-I/O control.
