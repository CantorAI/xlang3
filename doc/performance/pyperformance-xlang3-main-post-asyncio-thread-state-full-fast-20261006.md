# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **46** definitions and recorded **51** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **50** matched subtests, XLang3 was faster on **5**. The geometric mean of CPython time divided by XLang3 time was **0.17933×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

Against the previous same-version full run (0.17324×), this checkpoint improves the matched-subtest geometric mean by about **3.5%**. Most of that change comes from the asyncio task-state optimization; `async_tree_none` and `async_tree_eager` still run at only 0.063× and 0.068× CPython speed. The largest measured slowdown is `pickle_pure_python` at 0.053× (about 19× slower), followed by argparse subparsers at 0.055×. These are the next profiling targets; pure-Python standard-library code remains Python code, so any optimization must come from XLang3 execution/runtime behavior.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.svg)

## Run configuration

- XLang3 Release executable SHA-256: `A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`.
- XLang3 runtime DLL SHA-256: `BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.14 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30, async_tree=300, async_tree_eager=300.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `pickle_pure_python` | 273.5 µs | 5.192 ms | 0.053× |
| `subparsers` | 8.151 ms | 149.2 ms | 0.055× |
| `async_tree_none` | 227.4 ms | 3631 ms | 0.063× |
| `logging_silent` | 70.03 ns | 1.051 µs | 0.067× |
| `async_tree_eager` | 86.62 ms | 1280 ms | 0.068× |

Measured wins:

- `gc_traversal`: **1.935×** (1.205 ms vs 2.332 ms).
- `python_startup_no_site`: **1.164×** (17.78 ms vs 20.68 ms).
- `fannkuch`: **1.155×** (280.6 ms vs 324 ms).
- `pickle_list`: **1.116×** (4.144 µs vs 4.625 µs).
- `pickle_dict`: **1.086×** (24.33 µs vs 26.41 µs).

## Failure breakdown

- 32 definitions: Benchmark died.
- 19 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.json`](data/pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.json).
- Runner status log: [`pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006-stream.log`](data/pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006-stream.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
