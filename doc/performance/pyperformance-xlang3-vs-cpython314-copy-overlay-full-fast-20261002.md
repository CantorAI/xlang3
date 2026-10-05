# XLang3 vs CPython 3.14.7: corrected full pyperformance run

**Superseded by the current rebuilt Release run:** [updated all-97 comparison and horizontal chart](pyperformance-xlang3-gc-early-reject-vs-cpython314-full-fast-20261002.md). This earlier run is retained as a historical snapshot.

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **39** definitions and recorded **58** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **41** matched subtests, XLang3 was faster on **1**. The geometric mean of CPython time divided by XLang3 time was **0.14206×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-vs-cpython314-copy-overlay-full-fast-20261002.svg)

## Run configuration

- XLang3 Release executable SHA-256: `5F3E4D0F5ED27BFA0DC4F6D19863638CFCDA0B5275BC06F1A825DE7D521FD79C`.
- XLang3 runtime DLL SHA-256: `3A67FE039F835AA5C2714596253D9FEFFFBEFCA6F70A4B8C7E7926DF84DDB763`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 used the accessible Python 3.13 standard library because the configured Python 3.14 standard-library directory is inaccessible to this process.
- The XLang3 `PYTHONPATH` includes `scratch/performance-trials/python313-copy-compat`, which restores the `bytearray.copy` behavior expected by the Python 3.14 compatibility layer.
- Each XLang3 benchmark definition had a 120-second cap. The cap covers pyperf worker calibration and measurement together.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 328 ms | 0.018× |
| `subparsers` | 8.151 ms | 275.7 ms | 0.030× |
| `pickle_pure_python` | 273.5 µs | 6.281 ms | 0.044× |
| `logging_silent` | 70.03 ns | 1.468 µs | 0.048× |
| `unpickle_pure_python` | 205.3 µs | 4.055 ms | 0.051× |

Measured wins:

- `gc_traversal`: **1.368×** (1.705 ms vs 2.332 ms).

## Failure breakdown

- 35 definitions: Benchmark died.
- 23 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-vs-cpython314-copy-overlay-full-fast-20261002-all-97-status.csv) retains every benchmark definition and concise failure detail. The [matched subtest CSV](data/pyperformance-xlang3-vs-cpython314-copy-overlay-full-fast-20261002-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-copy-overlay-full-fast-20261002.json`](data/pyperformance-xlang3-copy-overlay-full-fast-20261002.json).
- Full run log and tracebacks: [`pyperformance-xlang3-copy-overlay-full-fast-20261002.log`](data/pyperformance-xlang3-copy-overlay-full-fast-20261002.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Previous run without the `bytearray.copy` overlay is retained separately; this report supersedes it for corrected compatibility status.

Benchmark worker failures have several causes: Python 3.13/3.14 standard-library and dependency mismatches (for example missing `distutils` and `annotationlib`), XLang3 native-module gaps, and interpreter compatibility bugs. See the failure-detail column and full log before attributing a failure to performance.
