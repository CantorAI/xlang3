# Skip untouched cache sites during VM frame cleanup (2026-10-03)

## Result

Rejected. I added an early return for cache records whose adaptive domain is
`Empty`, clearing their monitoring-generation and disabled-event fields before
returning. This would avoid zeroing the full cache record for a static cleanup
site that the activation never reached. The source change is removed.

Two 21-pair order-balanced comparisons showed no gain. Candidate/control was
**1.0034×** on `subparsers` (95% interval **0.9947–1.0177**; medians 243.037
ms vs 239.890 ms) and **1.0043×** on the pure-Python Pickler workload
(**0.9885–1.0290**; medians 51.135 ms vs 50.700 ms). Both intervals cross
parity and both point slightly slower. Output matched in every pair.

The measured result does not support this cleanup shortcut. The existing
ownership-aware per-domain cleanup remains unchanged; further work should
target larger shared interpreter costs rather than continue making small
variants of cache-site cleanup.

## Build identity and reproduction

Both builds used the same dirty working-tree source snapshot and MSVC Release
`/O2 /Ob3`; only the early-return check differed. The comparisons used
`C:\Python\Python314` (Python 3.14.7), five warmups, and 21 order-balanced
pairs via `benchmarks/diagnostics/measure_case_pair.py`.

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Control | `F04A35B4DBB32724431E9AA5803B1B966F56251539037BCABF4B5C3CF540A39E` | `452882D8DE64D5AA4AC8636E19820D66154F04B903DCBFF23906BFB55B5CF7B8` |
| Candidate | `F04A35B4DBB32724431E9AA5803B1B966F56251539037BCABF4B5C3CF540A39E` | `F09287763A6407AB157FA0FB8CCF5A5E27A71F63CCF8A6B8FC0D372EB7F0F359` |

Raw measurements: [`subparsers`](data/cache-empty-site-skip-subparsers-20261003.json)
and [pure-Python Pickler](data/cache-empty-site-skip-pickle-20261003.json).
The fixed `build-repro/Release` executable and runtime were not used or changed.
