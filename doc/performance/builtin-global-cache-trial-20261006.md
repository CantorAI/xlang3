# Builtin `LOAD_GLOBAL` cache trial (2026-10-06)

## Question

`LOAD_GLOBAL` already caches module globals, but a builtin fallback calls `Runtime::resolve_builtin` at each execution. The trial added a site cache for ordinary slots in the registered `builtins` module. It guarded the hit with globals and builtins module identities and versions, and preserved only scalar guards across frame reuse. Module namespace dictionary writes route through module setters and advance the module version, so direct `builtins.__dict__` mutation invalidates the cache.

## Correctness and build

The Release candidate built with MSVC and passed `xlang3_interpreter_tests.exe` and the complete `tests/run_fixtures.py` suite, including the dynamic builtins lookup fixture. The candidate source was reverted after measurement because results did not show a repeatable gain. The fixed Release executable and runtime DLL were restored from the recorded control pair; their SHA-256 hashes are `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548` and `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`.

## Results

All measurements used the repository's CPython 3.14.7-compatible pyperformance runner and the same Release control executable. Fast mode is a screen only; pyperf warned about unstable samples.

| Benchmark | Control | Candidate | Readout |
| --- | ---: | ---: | --- |
| `nqueens` | 808 ± 23 ms | 901 ± 247 ms | Candidate run was highly noisy; not significant |
| `pidigits` | 378 ± 16 ms | 374 ± 16 ms | pyperf hid the comparison as insignificant |
| `richards` (fast) | 342 ± 4 ms | 336 ± 2 ms | Reported 1.02× faster in this run |
| `richards` (rigorous, candidate before hit-path trimming) | 340 ± 12 ms | 336 ± 11 ms | 1.01× faster; unstable samples |
| `richards` (rigorous, trimmed hit path) | 340 ± 12 ms | 348 ± 21 ms | 1.02× slower; unstable samples |

The apparent `richards` improvement did not survive a repeated candidate run. Since the observed movement is within the large sample spread, this cache is not retained. It would add runtime state and module invalidation machinery without reliable evidence of a speedup.

## Raw evidence

- [Fast control JSON](data/pyperformance-builtin-global-cache-control-fast-20261006.json)
- [Fast candidate JSON](data/pyperformance-builtin-global-cache-candidate-fast-20261006.json)
- [Rigorous control JSON](data/pyperformance-builtin-global-cache-control-richards-rigorous-20261006.json)
- [First rigorous candidate JSON](data/pyperformance-builtin-global-cache-candidate-richards-rigorous-20261006.json)
- [Repeated rigorous candidate JSON](data/pyperformance-builtin-global-cache-candidate2-richards-rigorous-20261006.json)
