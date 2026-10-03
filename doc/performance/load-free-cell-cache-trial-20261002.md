# Guarded free-cell pointer cache (2026-10-02)

## Result

Retained as a small interpreter improvement. On the focused pure-Python
Pickler-shaped workload, the candidate was **1.6% faster** than the saved
current Release control across 31 order-balanced pairs:

| Build | Median | Candidate/control | Paired 95% interval |
| --- | ---: | ---: | ---: |
| Control | 47.483 ms | — | — |
| Candidate | 46.816 ms | 0.9842× | 0.9757–0.9892× |

This is a focused workload using fresh `pickle._Pickler` instances and repeated
`BytesIO` writes; it is not an official pyperformance `pickle_pure_python`
run. Treat the result as a measured microbenchmark gain, not proof that the
full CPython gap has materially closed.

## Why this path was selected

The opt-in VM profile for pyperformance's pure-Python Pickler recorded
392,028 `LoadFree` opcodes in one outer workload pass, among 2.5 million
profiled Value reference-count operations. Each `LoadFree` validates a closure
slot and converts its `Value` to a cell before loading the cell's current
value. The new per-instruction hint remembers the cell pointer after a
successful validation, but compares it against the current closure slot on
every hit. The fixture alternates two function objects created from the same
nested function body, then changes one nonlocal value and deletes it. This
checks that different activations use their own cells, writes remain visible,
and an unbound cell still follows the existing error path.

The cache stores a non-owning pointer. A live closure slot keeps the cell
alive, and the pointer is dereferenced only when the current slot has the same
object pointer. The implementation is documented beside `FreeSiteCache` and
the guarded `LoadFree` handler.

## Validation

- `xlang3_interpreter_tests.exe` passed with the Python 3.13 standard-library
  path configured.
- The new `load_free_cell_cache` fixture checks repeated reads, alternating
  closure cells, a `nonlocal` update visible through a warmed read site, and
  the unbound-cell `NameError` path after deletion. Control and candidate
  output both match the expected output.
- The complete fixed 11-case Release gate passed against the preserved
  `baseline-0336992` build. Per-case paired results are retained in
  [`load-free-cell-cache-fixed-release-gate-20261002.json`](data/load-free-cell-cache-fixed-release-gate-20261002.json).
- The isolated Pickler-shaped A/B samples, output digest, and build hashes are
  retained in
  [`load-free-cell-cache-pickle-writer-ab-20261002.json`](data/load-free-cell-cache-pickle-writer-ab-20261002.json).

The full fixture runner stops earlier at the existing
`runtime_protocol_fastcheck` expected-output mismatch. Running that fixture
with both the saved control and candidate produces identical output; neither
matches the checked-in expected file. The new closure fixture is validated
directly on both builds, and the unrelated fixture mismatch remains visible
rather than being reported as a pass.

The control executable/runtime hashes are `E0E46ED011542F262ADE92263679C5E4AABFDE86756F256CF2B1C0683406EC78` and
`11C5D5ED69E091CB5D1ED17B334A3C285267FE049243EFFC4FF107C7E5CA793C`. The
candidate hashes are `4B907E49B68F97164DC9459E59B2F5915297ABB6D73E2B7729F9E44356B64BE5` and
`CECF886D560F48108C6C73AE2D5250DA4D2D412B062D324E9B3B11F119D7E13B`.

The latest full pyperformance result is unchanged by this focused experiment:
three wins in 42 matched subtests, with a 0.16369× CPython/XLang3 geometric
mean. The goal remains to improve the remaining slow cases and failed
definitions; this cache is one small cumulative VM improvement.
