# Guarded free-cell pointer cache trial (2026-10-02)

## Decision

Rejected and removed. The first Pickler-shaped paired run appeared 1.6% faster
(0.9842× candidate/control; 95% interval 0.9757–0.9892), but it did not
establish that a free-cell cache caused the difference. Inspection of the
saved [`pickle.py` IR](../../scratch/performance/pickle-ir/pickle.ir.txt)
found no `LoadFree` instructions. The 392,028 `LoadFree` count in the earlier
VM-counter artifact aggregates the benchmark body and its Python dependencies;
it was incorrectly attributed to Pickler.

A direct closure microbenchmark executed exactly 200,000 `LoadFree` opcodes
per process and compared the saved pre-change Release control with the cache
candidate over 31 order-balanced pairs:

| Build | Median | Candidate/control | Paired 95% interval |
| --- | ---: | ---: | ---: |
| Control | 66.295 ms | — | — |
| Candidate | 66.183 ms | 0.9975× | 0.9886–1.0132× |

The interval crosses parity, so the optimization was removed. The earlier
Pickler-shaped difference remains recorded but is not claimed as a causal
speedup. Raw data for both runs is preserved in
[`the direct closure A/B`](data/load-free-cell-cache-direct-ab-20261002.json)
and
[`the Pickler-shaped A/B`](data/load-free-cell-cache-pickle-writer-ab-20261002.json).

## Closure correctness coverage

The `closure_cell_semantics` fixture remains as general closure-semantics
coverage: it alternates two function objects created from the same nested
function body, changes one captured variable through `nonlocal`, then deletes
it and checks for `NameError`. Both the saved control and the former candidate
produce the expected output. `xlang3_interpreter_tests.exe` passed with the
Python 3.13 standard-library path configured.

The full fixture runner stops earlier at the existing
`runtime_protocol_fastcheck` expected-output mismatch. Running that fixture
on both saved builds produces identical output, but neither matches the
checked-in expected file.

The 11-case fixed Release gate also passed after removing the cache. The
restored candidate executable/runtime hashes are
`DC0976BB54FBB2AC448E2431FC508DDCB17A9EDD994AF2D01FA075B1A7950F41` and
`804C4519F36B8A22EC57FD8E759DDAAA0D88BC58411F4BA67B7A7D081B655885`. Per-case
results are retained in
[`the rollback fixed-gate data`](data/load-free-cell-cache-rollback-fixed-release-gate-20261002.json).
The earlier gate for the former candidate with its cache enabled remains
historical in
[`the fixed-gate data`](data/load-free-cell-cache-fixed-release-gate-20261002.json).
The latest full pyperformance report remains three wins in 42 matched subtests
with a 0.16369× CPython/XLang3 geometric mean.
