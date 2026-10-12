# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. Its workers completed **61** definitions and recorded **36** failures/timeouts. Benchmark failures make the suite unsuccessful; every definition was attempted.

Of **70** matched subtests, XLang3 was faster on **2**. The geometric mean of CPython time divided by XLang3 time was **0.14013×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.svg)

## Run configuration

- XLang3 Release executable SHA-256: `7D9FB86BB78EA8357107C92E39253E16C9BAE60BBB2235AE358E83C35580DF39`.
- XLang3 runtime DLL SHA-256: `F366FA18ABD77D98DE6FD739C2F17850FBEFC2C7178CE4086F996F578901BC4E`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `C:\Python\Python314\Lib (Python 3.14.7)`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 300-second cap covering pyperf worker calibration and measurement. Overrides: none.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `typing_runtime_protocols` | 128.1 µs | 2.861 ms | 0.045× |
| `async_tree_none` | 229.3 ms | 4986 ms | 0.046× |
| `async_tree_eager_memoization_tg` | 266 ms | 5674 ms | 0.047× |
| `async_tree_memoization_tg` | 275.8 ms | 5527 ms | 0.050× |
| `async_tree_memoization` | 278.5 ms | 5496 ms | 0.051× |

Nominal timing wins (not proof of statistical significance or correctness):

- `fannkuch`: **1.125×** (281.9 ms vs 317.2 ms).
- `gc_traversal`: **1.014×** (2.111 ms vs 2.14 ms).

## Failure breakdown

- 26 definitions: Benchmark died.
- 10 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147-subtests.csv) contains raw per-subtest means and speed ratios.
Partial values from failed definitions remain in raw evidence and are excluded from timing tables, speed ratios and aggregate statistics.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.pyperf.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.pyperf.json).
- Runner status log: [`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.log`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython3147-live-eval-full-fast-20261007.json`](data/pyperformance-cpython3147-live-eval-full-fast-20261007.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
