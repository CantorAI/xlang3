# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **47** definitions and recorded **50** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **51** matched subtests, XLang3 was faster on **5**. The geometric mean of CPython time divided by XLang3 time was **0.16747×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.svg)

## Run configuration

- XLang3 Release executable SHA-256: `DC0976BB54FBB2AC448E2431FC508DDCB17A9EDD994AF2D01FA075B1A7950F41`.
- XLang3 runtime DLL SHA-256: `6476C3F62F765768D72A9984886DA32EE15C117D41AB466C750AB55BB6C59490`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.13 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30s, async_tree_eager=300s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 247.4 ms | 0.023× |
| `async_tree_eager` | 86.62 ms | 3107 ms | 0.028× |
| `pickle_pure_python` | 273.5 µs | 5.148 ms | 0.053× |
| `subparsers` | 8.151 ms | 153.1 ms | 0.053× |
| `logging_silent` | 70.03 ns | 1.102 µs | 0.064× |

Measured wins:

- `gc_traversal`: **1.934×** (1.206 ms vs 2.332 ms).
- `fannkuch`: **1.174×** (276 ms vs 324 ms).
- `pickle_list`: **1.173×** (3.944 µs vs 4.625 µs).
- `python_startup_no_site`: **1.167×** (17.72 ms vs 20.68 ms).
- `pickle_dict`: **1.049×** (25.19 µs vs 26.41 µs).

## Failure breakdown

- 29 definitions: Benchmark died.
- 21 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003-subtests.csv) contains raw per-subtest means and speed ratios.

The 29 worker deaths have concrete compatibility causes in the status CSV and runner log. Examples include parse failures in `docutils.utils.smartquotes`, `tornado.escape`, and `sqlalchemy.sql.selectable`; missing `distutils`; missing SQLite `create_aggregate`; and runtime errors in generic aliases, generator expressions, and exception formatting. These are interpreter/module compatibility work, not slow benchmark timings. The 21 timeouts are separate; most are async-tree cases, with `base64`, `bpe_tokeniser`, `pprint`, and `tomli_loads` also reaching their caps.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.json`](data/pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.json).
- Runner status log: [`pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.log`](data/pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
