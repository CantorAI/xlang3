# Exact-call argument ownership-transfer trial (2026-10-02)

## Hypothesis

CPython 3.14 transfers exact positional argument references from its value
stack into interpreter-frame locals. XLang3 copies each argument into the
callee's local slots, then releases dead caller registers during cleanup. I
tested transferring only positional object values whose source register's
last-use metadata points at that call instruction. Keyword, expanded,
signature-binding, and still-live argument paths retained their ordinary
copying semantics; repeated references were copied from the first transferred
argument so aliasing stayed intact.

The existing
[`call_argument_last_use_transfer` fixture](../../tests/fixtures/core/call_argument_last_use_transfer.py)
passed with the trial enabled. It checks identity, repeated arguments, use of a
value after a call, generator arguments, and argument-binding failure.

## Paired Release result

The candidate and parent were built from the same dirty checkout and build
configuration; the only source delta was this transfer trial. The regression
runner used seven order-balanced pairs and one warmup, with identical benchmark
inputs and output checks. Candidate/baseline ratios below 1.0 favor the
candidate.

| Case | Parent | Candidate | Candidate / parent | 95% paired interval |
|---|---:|---:|---:|---:|
| `function_calls` | 0.922 ms | 0.911 ms | 0.997× | 0.975–1.008× |
| `subparsers` | 256.477 ms | 261.186 ms | 1.022× | 1.005–1.030× |

The common function-call case is statistically neutral. The Python-heavy
`subparsers` case regressed by 2.2%, with its paired interval wholly above
1.0×. The reference-transfer branch was therefore removed. Do not count this
as a performance gain or repeat the same exact-call transfer without a new
mechanism that addresses the measured extra work.

The [raw paired report](data/arg-transfer-parent-vs-candidate-20261002.json)
records all samples, outputs, executable/runtime hashes, and intervals. The
separate [fixed-baseline screen](data/arg-transfer-current-vs-fixed-baseline-20261002.json)
compares the pre-rollback candidate with the older preserved baseline; it
includes accumulated changes and cannot attribute their gains to this trial.
