# Adaptive cache hit-counter trial (2026-10-01)

CPython's warmed inline caches stop doing adaptive work after a site
specializes. XLang3's cache helper still increments `hit_count` on specialized
hits, although the VM reads that counter only while training a site. I tested
saturating the counter when a site is promoted, so the existing counter guard
would stop writing it without adding another branch to every cache hit.

The unchanged official pyperformance `unpickle_pure_python --fast` benchmark
measured **3.15 ±0.06 ms** for both the parent and candidate. `pyperf
compare_to` hid the result as statistically insignificant. The candidate is
removed; this counter write is not a material part of the measured slowdown,
so it should not be revisited as a standalone optimization.

| Runtime | `xlang3.exe` SHA-256 | Runtime DLL SHA-256 | Result |
|---|---|---|---|
| Parent | `8B514A12CB91479CD67591F9D2BA159DF33FED36E913EDF3E67585CC33DAB1DA` | `B80E9EE7CDFE171CC89689050BC75C9A32D2DC606FF1D5005A05552FB6374624` | 3.15 ±0.06 ms |
| Candidate | `8B514A12CB91479CD67591F9D2BA159DF33FED36E913EDF3E67585CC33DAB1DA` | `122FEDEA5285576368F23053DBB21B71EF690F6566007E43C0DA5891138C5630` | 3.15 ±0.06 ms |

Raw pyperf data: [parent](data/cache-hit-counter-parent-fast-20261001.json),
[candidate](data/cache-hit-counter-candidate-fast-20261001.json).
