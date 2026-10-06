# Small-integer dictionary hash trial (2026-10-06)

## Result

I tested bypassing the general integer hash mixer when a nonnegative key fits
directly in the dictionary's open-addressed table. This was motivated by the
13.84% instrumented self-time attributed to `GetItem` in the pure-Python
unpickling profile. The pyperformance results did not validate the change:

| Benchmark | Control fast | Candidate fast | Control rigorous | Candidate rigorous |
|---|---:|---:|---:|---:|
| `pickle_pure_python` | 5.23 ms ± 0.23 ms | 5.17 ms ± 0.05 ms | — | — |
| `unpickle_pure_python` | 2.38 ms ± 0.18 ms | 2.32 ms ± 0.12 ms | 2.32 ms ± 0.11 ms | 2.34 ms ± 0.17 ms |

`pyperf compare_to` classified the two fast-mode comparisons as insignificant.
The rigorous unpickling candidate was slightly slower than control, also within
noise. All runs reported fast-mode stability warnings. The candidate was
reverted; no speedup is claimed.

This result means the interpreter-level `GetItem` profile share does not by
itself identify integer hash mixing as the dominant cost. The opcode includes
other dispatch, cache, bounds, and reference-management work, so changing the
hash calculation alone did not move the end-to-end benchmark measurably.

## Validation and build identity

The candidate passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and the complete fixture runner using
`C:\Python\Python314\python.exe` (CPython 3.14.7). The fixed Release pair has
been restored:

- Executable SHA-256: `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Runtime DLL SHA-256: `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`
- Candidate executable SHA-256: `9B3B26AFCBE7DA4B2ACE03F6BFF422151CBF7DACE845AC8D039AACB54B55F077`
- Candidate runtime DLL SHA-256: `3F384B072F0385EDD76AD5B6F7B303854E6A3DEE7AE6CCBE07C3D4E0FD2F74CD`

All measurements used pyperformance 1.14.0, Python 3.14.7, the same Windows
compatibility shim and dependency site, and the same fixed Release control.
Fast mode used 20 measured values across 10 workers; rigorous mode used the
official pyperformance rigorous settings.

## Raw evidence

- [Control fast JSON](data/integer-identity-hash-control-fast-20261006.json)
- [Candidate fast JSON](data/integer-identity-hash-candidate-fast-20261006.json)
- [Control rigorous JSON](data/integer-identity-hash-control-rigorous-20261006.json)
- [Candidate rigorous JSON](data/integer-identity-hash-candidate-rigorous-20261006.json)
