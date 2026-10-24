# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **52** definitions and recorded **45** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **56** matched subtests, XLang3 was faster on **4**. The geometric mean of CPython time divided by XLang3 time was **0.15710×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.svg)

## Run configuration

- XLang3 Release executable SHA-256: `2AE30FA9A56B11E9BF3298D5A48CA33357C9FF93C097FD29A8B9D485769A1E63`.
- XLang3 runtime DLL SHA-256: `FD3BB2E9FFAFC38B501D6FA6A9AD5DE7A5AAB38792EAF86D40835CF4AE70C44D`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.14 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30s, async_tree_eager=300s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 186.1 ms | 0.031× |
| `async_tree_eager` | 86.62 ms | 2727 ms | 0.032× |
| `sqlglot_v2_parse` | 1.01 ms | 20.74 ms | 0.049× |
| `sqlglot_v2_transpile` | 1.302 ms | 24.35 ms | 0.053× |
| `pickle_pure_python` | 273.5 µs | 5.074 ms | 0.054× |

Measured wins:

- `gc_traversal`: **1.954×** (1.194 ms vs 2.332 ms).
- `fannkuch`: **1.244×** (260.4 ms vs 324 ms).
- `pickle_list`: **1.159×** (3.99 µs vs 4.625 µs).
- `pickle_dict`: **1.073×** (24.62 µs vs 26.41 µs).

## Failure breakdown

- 23 definitions: Benchmark died.
- 22 definitions: Benchmark timed out.

The [all-97 status CSV](performance/pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](performance/pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-context-field-index-cache-full-fast-20261004.json`](performance/data/pyperformance-xlang3-context-field-index-cache-full-fast-20261004.json).
- Runner status log: [`pyperformance-xlang3-context-field-index-cache-full-fast-20261004.log`](performance/data/pyperformance-xlang3-context-field-index-cache-full-fast-20261004.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](performance/data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
