# XLang3 vs CPython 3.14.7: corrected full pyperformance run

The corrected XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. It completed **45** definitions and recorded **52** failures/timeouts. The command returned exit code 1 because pyperformance treats those benchmark failures as an unsuccessful suite; every definition was attempted.

Of **49** matched subtests, XLang3 was faster on **5**. The geometric mean of CPython time divided by XLang3 time was **0.16188×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-final-state-shimmed-vs-cpython314-fast-20261004.svg)

## Run configuration

- XLang3 Release executable SHA-256: `F4929AF5873C995AE038A30F5DAF5AB361B321A8FE777F655C7DD3127158E68F`.
- XLang3 runtime DLL SHA-256: `E4D81CF016BB520228AFFA495411476698F8274944E25F8261499AB606C53998`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `Python 3.14 standard library`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 120-second cap covering pyperf worker calibration and measurement. Overrides: async_tree*=30, async_tree_eager=300.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `pickle_pure_python` | 273.5 µs | 5.426 ms | 0.050× |
| `subparsers` | 8.151 ms | 159.9 ms | 0.051× |
| `async_tree_eager` | 86.62 ms | 1585 ms | 0.055× |
| `logging_silent` | 70.03 ns | 1.224 µs | 0.057× |
| `unpickle_pure_python` | 205.3 µs | 3.516 ms | 0.058× |

Measured wins:

- `gc_traversal`: **1.783×** (1.308 ms vs 2.332 ms).
- `pickle_list`: **1.140×** (4.057 µs vs 4.625 µs).
- `fannkuch`: **1.126×** (287.7 ms vs 324 ms).
- `python_startup_no_site`: **1.059×** (19.54 ms vs 20.68 ms).
- `pickle_dict`: **1.051×** (25.13 µs vs 26.41 µs).

## Failure breakdown

- 32 definitions: Benchmark died.
- 20 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-final-state-shimmed-vs-cpython314-fast-20261004-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-final-state-shimmed-vs-cpython314-fast-20261004-subtests.csv) contains raw per-subtest means and speed ratios.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-final-state-shimmed-fast-20261004.json`](data/pyperformance-xlang3-final-state-shimmed-fast-20261004.json).
- Runner status log: [`pyperformance-xlang3-final-state-shimmed-fast-20261004.log`](data/pyperformance-xlang3-final-state-shimmed-fast-20261004.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.
