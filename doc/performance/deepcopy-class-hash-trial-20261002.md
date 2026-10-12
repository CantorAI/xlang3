# Class-object hash fast path for `deepcopy` (2026-10-02)

## Change and result

CPython 3.14's pure-Python `copy.deepcopy` checks `cls in _atomic_types` on
each recursive visit. XLang3 already hashes class objects by object identity,
but `value_hash_key` reached that identity fallback only after checking the
numeric, string, bytes, complex, memoryview, and tuple cases. This trial adds
an early `ObjectKind::Class` branch that returns the same identity hash. It
does not move `copy.py` into native code or change class hashing semantics.

The official pyperformance 1.14 `deepcopy` definition, run with pyperf 2.10
in rigorous mode, measured these XLang3 control/candidate results:

| Subtest | Clean XLang3 control | Candidate | Candidate speedup |
| --- | ---: | ---: | ---: |
| `deepcopy` | 4.60 ms ± 0.46 ms | 2.99 ms ± 0.30 ms | 1.53× |
| `deepcopy_reduce` | 44.8 µs ± 4.7 µs | 31.0 µs ± 2.3 µs | 1.45× |
| `deepcopy_memo` | 574 µs ± 59 µs | 319 µs ± 29 µs | 1.80× |
| Geometric mean | — | — | **1.59×** |

CPython 3.14.7 measured `deepcopy_memo` at 23.8 µs ± 2.2 µs in the same
rigorous runner, so XLang3 is still about **13.4× slower** on this subtest.
Pyperf warns that the samples are not stable to a 1% threshold. The
candidate/control gap is large and consistent across all three subtests, but
these figures should be read alongside the complete benchmark and fixed-gate
runs below.

A separate `set_membership_scaling.py` microbenchmark measured absent
class-object lookups in a warmed set at about 1,000 ns before the fast path
and 57–92 ns after it. CPython measured 15–19 ns. This probe explains the
direction and size of the change but is not a substitute for pyperformance.

## Correctness and reproducibility

`tests/cpp/runtime_value_tests.cpp` checks that the class-object hash remains
exactly `std::hash<const void*>(class_object)`. The C++ runtime value test
passes. The production code comment records why this identity-hash path must
stay ahead of generic kind checks.

Raw official pyperformance results:

- XLang3 clean control: [`deepcopy-class-hash-control-rigorous-20261002.json`](data/deepcopy-class-hash-control-rigorous-20261002.json)
- XLang3 candidate: [`deepcopy-class-hash-candidate-rigorous-20261002.json`](data/deepcopy-class-hash-candidate-rigorous-20261002.json)
- CPython 3.14.7: [`deepcopy-class-hash-cpython314-rigorous-20261002.json`](data/deepcopy-class-hash-cpython314-rigorous-20261002.json)
- Fast-mode screening runs: [`control`](data/deepcopy-class-hash-control-fast-20261002.json), [`candidate`](data/deepcopy-class-hash-candidate-fast-20261002.json), [`CPython`](data/deepcopy-class-hash-cpython314-fast-20261002.json)
- Fixed Release gate: [`deepcopy-class-hash-fixed-baseline-gate-20261002.json`](data/deepcopy-class-hash-fixed-baseline-gate-20261002.json)
- Refreshed full suite: [all-97 status](data/pyperformance-xlang3-class-hash-full-fast-20261002-all-97-status.csv), [subtests](data/pyperformance-xlang3-class-hash-full-fast-20261002-subtests.csv), [chart](pyperformance-xlang3-class-hash-full-fast-20261002.svg), and [raw XLang3 pyperf JSON](data/pyperformance-xlang3-class-hash-full-fast-20261002.json)

The saved clean control and candidate use the same executable SHA-256
`EF20D5E34C5F36F7C7D40FB152311C3449CA505980DAD963CEEBC859578A94A2`; the
runtime DLLs differ: control
`BD61627CA60E44E870781C9129F9FFFB5EFECA96A14354568024E90ECB99E153`,
candidate `0C90E9AF35D62889095B6ABDAE29C0E239EE103DE4AE19065290102AD42D7095`.

The refreshed full run attempted all 97 definitions: 50 XLang3 subtests across
46 definitions were measured, and 51 definitions failed or timed out. Of 50
matched subtests, 3 were faster than CPython and 47 slower; the geometric
CPython/XLang3 ratio was **0.15514×**. This fast-mode aggregate is not an
overall speedup or a controlled paired suite comparison. The individual
samples have high variation, and the matched set differs from the clean
control run. `deepcopy_memo` measured **320 µs** in the full pass, consistent
with the focused XLang3 A/B result.

The full runner returned exit code 1 because of benchmark failures. The
adjacent `*-status.log` is a reconstructed case-status artifact used to build
the CSV and chart, not a verbatim capture of the runner's full stdout. The raw
pyperf JSON is the timing source; the attempt order and failure list came from
the completed runner output and final summary. Stock `pyperformance run`
failed on this host with pyperf's Windows `select` error 10038, so the full run
used the repository's Windows-safe `run_pyperformance_xlang3_shimmed.py`.
The fixed Release regression gate passes.
