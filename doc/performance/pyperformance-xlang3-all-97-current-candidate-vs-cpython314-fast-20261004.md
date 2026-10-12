# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **48** definitions and recorded **49** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **52** matched subtests, XLang3 was faster on **4**. The geometric mean of CPython time divided by XLang3 time was **0.16899×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-all-97-current-candidate-vs-cpython314-fast-20261004.svg)

## Run configuration

- XLang3 Release executable SHA-256: `57511DD72B472DB10C9A9D2ED3E5AE6F445F3433A04C045416BCCDC8E7567429`.
- XLang3 runtime DLL SHA-256: `8A35F064A2C6300BB9E46E0FEC0D4219D596B0D11B29D901ABF43685CEDDD599`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.14 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30s, async_tree_eager=300s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 185.9 ms | 0.031× |
| `async_tree_eager` | 86.62 ms | 2790 ms | 0.031× |
| `pickle_pure_python` | 273.5 µs | 5.131 ms | 0.053× |
| `subparsers` | 8.151 ms | 141.6 ms | 0.058× |
| `logging_silent` | 70.03 ns | 1.1 µs | 0.064× |

Measured wins:

- `gc_traversal`: **1.930×** (1.209 ms vs 2.332 ms).
- `fannkuch`: **1.220×** (265.6 ms vs 324 ms).
- `pickle_list`: **1.163×** (3.975 µs vs 4.625 µs).
- `pickle_dict`: **1.084×** (24.36 µs vs 26.41 µs).

## Failure breakdown

- 27 definitions: Benchmark died.
- 22 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-all-97-current-candidate-vs-cpython314-fast-20261004-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-all-97-current-candidate-vs-cpython314-fast-20261004-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-all-97-current-candidate-fast-20261004.json`](data/pyperformance-xlang3-all-97-current-candidate-fast-20261004.json).
- Runner status log: [`pyperformance-xlang3-all-97-current-candidate-failures-20261004.txt`](data/pyperformance-xlang3-all-97-current-candidate-failures-20261004.txt).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
