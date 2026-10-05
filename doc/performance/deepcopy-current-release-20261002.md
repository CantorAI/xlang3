# `deepcopy` on the current Release build (2026-10-02)

The official pyperformance 1.14.0 `deepcopy` definition completed in `--fast`
mode on the current Release executable. Its three subtests remain much slower
than CPython 3.14.7, while the `deepcopy_memo` class-hash optimization still
reduces the time relative to the saved clean XLang3 control.

| Subtest | CPython 3.14.7 | Current XLang3 | XLang3 slowdown | Clean XLang3 control | Speedup |
|---|---:|---:|---:|---:|---:|
| `deepcopy` | 225 µs | 3.77 ms | 16.74× | 4.45 ms | 1.18× |
| `deepcopy_reduce` | 2.34 µs | 36.9 µs | 15.77× | 43.4 µs | 1.18× |
| `deepcopy_memo` | 22.9 µs | 364 µs | 15.91× | 629 µs | 1.73× |
| Geometric mean | — | — | **16.14×** | — | **1.34×** |

The CPython reference and clean XLang3 control are the saved official fast-mode
results from the class-hash trial. The candidate `deepcopy_memo` time improved
from 629 µs to 364 µs. A follow-up attempt to remove a `type` name-map lookup
changed this subtest by less than 1% and did not produce a measurable pyperf
gain, so that additional runtime-state change was reverted.

The standard-library setup is constrained on this host: the configured
CPython 3.14 library directory is inaccessible. XLang3 therefore ran against
the available CPython 3.13 library with local compatibility overlays for
`bytearray.copy()` and the 3.14 `inspect.get_annotations()` lazy-annotation
behavior. The CPython 3.14.7 reference used its own standard library. Treat
this as useful current-binary evidence, not a fully identical stdlib setup.
Pyperf marked all three candidate subtests unstable at its strict 1% criterion.

The full 11-case local Release regression gate passed against the preserved
2026-09-29 executable. The output JSON records every case, paired sample, and
the executable/runtime hashes:
[`current-release-reverted-trial-vs-preserved-baseline-20261002.json`](data/current-release-reverted-trial-vs-preserved-baseline-20261002.json).

Raw pyperf results:

- [Current XLang3 `--fast`](data/deepcopy-final-reverted-fast-20261002.json)
- [CPython 3.14.7 reference](data/deepcopy-class-hash-cpython314-fast-20261002.json)
- [Clean XLang3 control](data/deepcopy-class-hash-control-fast-20261002.json)

The class-object identity hash rationale remains in [`value_hash.cpp`](../../src/runtime/value_hash.cpp), and the class-key and custom-metaclass semantics remain covered by `dict_get_missing_semantics.py`.
