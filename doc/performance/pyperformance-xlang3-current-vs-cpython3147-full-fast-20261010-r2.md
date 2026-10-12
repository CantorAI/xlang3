# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. Its workers completed **78** definitions and recorded **19** failures/timeouts. Benchmark failures make the suite unsuccessful; every definition was attempted.

Of **88** matched subtests, XLang3 was faster on **2**. The geometric mean of CPython time divided by XLang3 time was **0.12834×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2.svg)

## Run configuration

- XLang3 Release executable SHA-256: `2C9F7321C0B1048CFBC4E27ACFCEC7989712FD9014E136688173A0E9FC408CFF`.
- XLang3 runtime DLL SHA-256: `81C38FE0599D8A3821E834112477E4832C6DB3B1561392DA35BCE65AA2A7D387`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.14 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 300-second cap covering pyperf worker calibration and measurement. Overrides: networkx*=600s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `typing_runtime_protocols` | 128.1 µs | 2.895 ms | 0.044× |
| `coverage` | 65.51 ms | 1438 ms | 0.046× |
| `async_tree_eager_memoization_tg` | 266 ms | 5573 ms | 0.048× |
| `genshi_xml` | 42.5 ms | 868.4 ms | 0.049× |
| `sqlalchemy_imperative` | 11.24 ms | 225.9 ms | 0.050× |

Nominal timing wins (not proof of statistical significance or correctness):

- `fannkuch`: **1.132×** (280.3 ms vs 317.2 ms).
- `gc_traversal`: **1.037×** (2.064 ms vs 2.14 ms).

## Failure breakdown

- 9 definitions: Benchmark died.
- 10 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-current-vs-cpython3147-full-fast-20261010-r2-subtests.csv) contains raw per-subtest means and speed ratios.
Partial values from failed definitions remain in raw evidence and are excluded from timing tables, speed ratios and aggregate statistics.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-current-fixed-release-full-fast-20261010-r2.json`](data/pyperformance-xlang3-current-fixed-release-full-fast-20261010-r2.json).
- Runner status log: [`pyperformance-xlang3-current-fixed-release-full-fast-20261010-r2.log`](data/pyperformance-xlang3-current-fixed-release-full-fast-20261010-r2.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython3147-live-eval-full-fast-20261007.json`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.

- Fresh CPython status log: [`pyperformance-cpython3147-live-eval-full-fast-20261007.log`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.log).
