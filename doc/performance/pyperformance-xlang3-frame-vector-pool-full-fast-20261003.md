# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **47** definitions and recorded **50** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **51** matched subtests, XLang3 was faster on **3**. The geometric mean of CPython time divided by XLang3 time was **0.14957×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

Compared with the previous same-version full run, the geometric mean moved from **0.14699×** to **0.14957×** and the win count stayed at three. The focused `async_tree_eager` result improved, but across the full matched set this is a small overall shift, not a broad reversal of the performance gap. See the [frame-vector reuse trial note](async-tree-frame-vector-pool-trial-20261003.md) for the focused measurement and implementation details.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-frame-vector-pool-full-fast-20261003.svg)

## Run configuration

- XLang3 Release executable SHA-256: `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.
- XLang3 runtime DLL SHA-256: `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `C:\Python\Python314\Lib`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30s, async_tree_eager=300s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `telco` | 5.755 ms | 213.6 ms | 0.027× |
| `async_tree_eager` | 86.62 ms | 3165 ms | 0.027× |
| `pickle_pure_python` | 273.5 µs | 5.819 ms | 0.047× |
| `subparsers` | 8.151 ms | 173.2 ms | 0.047× |
| `unpickle_pure_python` | 205.3 µs | 3.543 ms | 0.058× |

Measured wins:

- `gc_traversal`: **1.661×** (1.404 ms vs 2.332 ms).
- `python_startup_no_site`: **1.070×** (19.34 ms vs 20.68 ms).
- `fannkuch`: **1.022×** (317.2 ms vs 324 ms).

## Failure breakdown

- 29 definitions: Benchmark died.
- 21 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-frame-vector-pool-full-fast-20261003-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-frame-vector-pool-full-fast-20261003-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-frame-vector-pool-full-fast-20261003.json`](data/pyperformance-xlang3-frame-vector-pool-full-fast-20261003.json).
- Runner status log: [`pyperformance-xlang3-frame-vector-pool-full-fast-20261003.log`](data/pyperformance-xlang3-frame-vector-pool-full-fast-20261003.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
