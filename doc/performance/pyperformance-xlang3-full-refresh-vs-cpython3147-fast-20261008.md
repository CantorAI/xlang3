# Fresh XLang3 all-97 attempt versus saved CPython 3.14.7

The CPython reference was measured on October 7, 2026; XLang3 is freshly measured. This is an **unpaired saved-reference comparison**. CPython EXE/DLL, benchmark Python sources, hook and dependency metadata are pinned, but historical metadata alone does not prove every historical package/data byte.

The XLang3 run attempted all **97** pyperformance 1.14.0 definitions in `--fast` mode. Its workers completed **73** definitions and recorded **24** failures/timeouts. Benchmark failures make the suite unsuccessful; every definition was attempted.

Of **81** matched subtests, XLang3 was faster on **1**. The geometric mean of CPython time divided by XLang3 time was **0.12655×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence. This aggregate covers the 81 scored successful subtests only; it is not a whole-suite speed score.

![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3](pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008.svg)

## Run configuration

- XLang3 Release executable SHA-256: `75e9d021b1be889295e81f969417e601e936d89f2dcb80b6ec71b1032fd25974`.
- XLang3 runtime DLL SHA-256: `529c8c8413da804eb576c709bc7ee08bfa048b6f6a093c67235a9e5ede5d58c4`.
- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `C:/Python/Python314/Lib`.
- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.
- Each XLang3 benchmark definition had a 300-second cap covering pyperf worker calibration and measurement. Overrides: networkx*=600s.

## Largest slowdowns and wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `typing_runtime_protocols` | 128.1 µs | 2.84 ms | 0.045× |
| `pickle_pure_python` | 257.5 µs | 5.451 ms | 0.047× |
| `async_tree_eager_memoization_tg` | 266 ms | 5608 ms | 0.047× |
| `genshi_xml` | 42.5 ms | 888.7 ms | 0.048× |
| `async_tree_memoization_tg` | 275.8 ms | 5459 ms | 0.051× |

Nominal timing wins (not proof of statistical significance or correctness):

- `fannkuch`: **1.176×** (269.8 ms vs 317.2 ms).

## Failure breakdown

- 13 definitions: Benchmark died.
- 11 definitions: Benchmark timed out.

The [all-97 status CSV](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-all-97-status.csv) retains every benchmark definition and failure status. The [matched subtest CSV](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-subtests.csv) contains raw per-subtest means and speed ratios.
Partial values from failed definitions remain in raw evidence and are excluded from timing tables, speed ratios and aggregate statistics.

## Raw evidence

- XLang3 pyperf JSON: [`pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-xlang.json`](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-xlang.json).
- Runner status log: [`pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-xlang.log`](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-xlang.log).
- CPython 3.14.7 pyperf JSON: [`pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-cpython3147.json`](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-cpython3147.json).
- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.

Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.

- Saved CPython status log: [`pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-cpython3147.log`](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-input-cpython3147.log).

## Workload correctness exclusions

Worker completion does not prove equal work. The following raw timings remain in the CSV, but have no speed ratio and are excluded from the chart, win count and geometric mean:

- `gc_traversal`: Traversal work remains uncertified; retain raw status/timing and withhold speed score pending independent current workload coverage.

Evidence: [pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-correctness-exclusions.json](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-correctness-exclusions.json).

## Saved reference dates and input identity

CPython completed all 97 definitions / 124 subtests on 2026-10-08T01:02:03.676413+00:00 through 2026-10-08T01:32:59.043022+00:00. The fresh XLang3 run began 2026-10-08T12:02:18.228910+00:00 and ended 2026-10-08T14:17:03.495257+00:00. These are **unpaired saved-reference comparisons**, not paired optimization gains. CPython throughput is 1x; the chart uses CP time / XLang3 time, with values over 1x favoring XLang3. Fast-mode variation/warnings remain.

Exact CPython EXE/DLL, manager versions, compatibility hook, all 224 recorded benchmark Python sources and 82 dependency METADATA identities match the saved October 7 reference. Current benchmark/dependency source, data and native bytes were additionally pinned before/after the fresh run. **Historical package METADATA does not prove every dependency source/data byte was unchanged since October 7.** The existing 43-source SQL audit covers that SQL scope only; no broader retrospective package-source equivalence is asserted.

[All raw values](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-all-raw-values.csv) and [sample variation](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-sample-variation.csv) retain every full-JSON timing, including clearly marked unscored failed definitions/workloads. [Failed partial snapshots](data/pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008-failed-partial-subtests.csv) remain separate and unscored; invalid/truncated files retain index rows and exact raw bytes. No values/outliers are trimmed or pooled.

Worker completion and nominal wins do not certify equal work in every benchmark. GC traversal remains conservatively unscored pending current independent workload coverage. ElementTree variants retain their original recorded names and backend metadata in raw JSON; accelerator/pure-Python variants are not renamed.
