# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **45** definitions and recorded **52** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **49** matched subtests, XLang3 was faster on **5**. The geometric mean of CPython time divided by XLang3 time was **0.16492×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-call-keyword-binder-vs-cpython314-fast-20261004.svg)

## Run configuration

- XLang3 Release executable SHA-256: `CDDF039187CE8AFDA6D4FDAFAA32AB0617BCAEBFBDB8D8868D0CC5EE4F85C3BB`.
- XLang3 runtime DLL SHA-256: `71CECF7C8579DC181BF596FA348492E4C6B3E81249086F2031B4CF006E711236`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `C:\Python\Python314\Lib`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30s, async_tree_eager=300s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 192.1 ms | 0.030× |
| `async_tree_eager` | 86.62 ms | 2826 ms | 0.031× |
| `pickle_pure_python` | 273.5 µs | 5.137 ms | 0.053× |
| `subparsers` | 8.151 ms | 145.5 ms | 0.056× |
| `logging_silent` | 70.03 ns | 1.106 µs | 0.063× |

Measured wins:

- `gc_traversal`: **1.970×** (1.184 ms vs 2.332 ms).
- `fannkuch`: **1.201×** (269.7 ms vs 324 ms).
- `python_startup_no_site`: **1.147×** (18.03 ms vs 20.68 ms).
- `pickle_list`: **1.110×** (4.166 µs vs 4.625 µs).
- `pickle_dict`: **1.027×** (25.71 µs vs 26.41 µs).

## Failure breakdown

- 32 definitions: Benchmark died.
- 20 definitions: Benchmark timed out.

The [all-97 status CSV](performance/pyperformance-xlang3-call-keyword-binder-vs-cpython314-fast-20261004-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](performance/pyperformance-xlang3-call-keyword-binder-vs-cpython314-fast-20261004-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-call-keyword-binder-all-97-fast-20261004.json`](performance/data/pyperformance-xlang3-call-keyword-binder-all-97-fast-20261004.json).
- Runner status log: [`pyperformance-xlang3-call-keyword-binder-all-97-fast-20261004.log`](performance/data/pyperformance-xlang3-call-keyword-binder-all-97-fast-20261004.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](performance/data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
