# Current Release regression gate (2026-10-02)

The full default 11-case Release gate passed against the preserved 2026-09-29
Release pair. Ratios below are candidate time divided by baseline time, so
values below 1.0 mean the current candidate is faster. The gate uses 21
order-balanced pairs per case and its default 10% slowdown tolerance.

| Case | Candidate / baseline | Baseline | Candidate |
|---|---:|---:|---:|
| `local_slots` | 0.391× | 39.28 ms | 14.94 ms |
| `scalar_arithmetic` | 0.995× | 9.09 ms | 9.11 ms |
| `range_for` | 1.010× | 1.27 ms | 1.27 ms |
| `function_calls` | 1.008× | 0.99 ms | 1.00 ms |
| `class_construct` | 0.986× | 10.56 ms | 10.39 ms |
| `list_append` | 0.988× | 0.41 ms | 0.42 ms |
| `property_access` | 1.001× | 0.95 ms | 0.94 ms |
| `deepcopy_memo` | 0.555× | 14.31 ms | 8.10 ms |
| `json_dumps` | 0.827× | 46.69 ms | 38.66 ms |
| `gc_traversal` | 0.977× | 7.82 ms | 7.64 ms |
| `subparsers` | 0.868× | 319.69 ms | 278.97 ms |

The run exited 0. The raw JSON, including all samples and output checks, is
[`current-release-vs-preserved-baseline-20261002.json`](data/current-release-vs-preserved-baseline-20261002.json).

Baseline executable and runtime SHA-256 values:

- `xlang3.exe`: `08ecbee1372891dd52e668942a4d925e8f8c68bbc3234aa1f014f616a6526739`
- `xlang3_runtime.dll`: `9642e92eeef5be77a6f7622a837b56954ed0965561bbf611aad7d216e170e14f`

Candidate executable and runtime SHA-256 values:

- `xlang3.exe`: `0c3c75c0cbd7229900d89a7f9f50365b4e35472bc251b750af297b6cf126b11c`
- `xlang3_runtime.dll`: `5c9db488dbd44a17e5b8ebba27e2e05fd0abfdd8fdb4edc8d92811db24dcc491`

The harness ran under Python 3.13 with the local `bytearray.copy()` compatibility
overlay needed to load the configured Python library. This is a regression
gate against the preserved September executable, not a CPython 3.14
comparison, a full pyperformance result, or evidence that August performance
has been restored.

## Current branch rerun against the fixed baseline

The current `build-repro/Release` candidate was rerun against
`scratch/performance/baseline-0336992/xlang3.exe` with the same compatibility
overlay and 41 order-balanced pairs. Ten cases passed; `list_append` was
inconclusive because its interval crossed the gate threshold despite a median
ratio near 1.0. A separate 101-pair run of that case passed. Ratios below are
candidate / baseline (lower is faster):

| Case | Ratio | 95% paired interval | Pairs |
|---|---:|---:|---:|
| `local_slots` | 0.381× | 0.363–0.401 | 41 |
| `scalar_arithmetic` | 0.993× | 0.979–1.008 | 41 |
| `range_for` | 1.025× | 1.004–1.047 | 41 |
| `function_calls` | 0.979× | 0.956–1.002 | 41 |
| `class_construct` | 0.998× | 0.974–1.023 | 41 |
| `list_append` | 0.997× | 0.954–1.013 | 101 |
| `property_access` | 0.959× | 0.927–0.995 | 41 |
| `deepcopy_memo` | 0.562× | 0.556–0.567 | 41 |
| `json_dumps` | 0.829× | 0.793–0.864 | 41 |
| `gc_traversal` | 1.022× | 0.967–1.076 | 41 |
| `subparsers` | 0.873× | 0.868–0.878 | 41 |

All eleven cases pass when the independently confirmed longer `list_append`
result is used. This is a branch-to-XLang3-baseline check; the ratios include
all changes accumulated since that baseline and do not attribute speedups to
the latest hash-path correction. Raw evidence is in
[`current-release-fixed-baseline-r41-20261002.json`](data/current-release-fixed-baseline-r41-20261002.json)
and
[`current-release-list-append-fixed-baseline-r101-20261002.json`](data/current-release-list-append-fixed-baseline-r101-20261002.json).
