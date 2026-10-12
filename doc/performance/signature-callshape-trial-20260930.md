# Cached exact-positional call shape trial (2026-09-30)

## Result

Precomputing the function signature category during lowering and IR decoding
did not produce a repeatable pyperformance gain. The change was removed, and
no full Release regression gate was run.

| Benchmark | Order | Control | Candidate | Comparison |
|---|---|---:|---:|---|
| `unpickle_pure_python` | Candidate, then control | 3.47 ± 0.04 ms | 3.50 ± 0.04 ms | 1.01× slower |
| `deltablue` | Candidate, then control | 65.2 ± 3.1 ms | 62.8 ± 2.3 ms | 1.04× faster |
| `deltablue` | Control, then candidate | 64.7 ± 4.4 ms | 64.3 ± 5.2 ms | No significant difference |

All `deltablue` runs warned that host variation made the results unstable.
The first apparent gain disappeared in the reverse-order repeat. This does
not justify retaining more IR metadata and its decode/lowering maintenance.

The unchanged benchmarks were pyperformance 1.14.0's
`bm_pickle/run_benchmark.py --rigorous --pure-python --protocol 5 unpickle`
and `bm_deltablue/run_benchmark.py --rigorous`, with pyperf 2.10.0 on the same
Windows host. The candidate passed the IR codec round-trip test and complete
Python fixture runner, but failed the performance acceptance check.

## Hypothesis and implementation tested

The [CPython 3.14.7 VM comparison](cpython314-vm-comparison-20260930.md)
shows CPython specializing the warmed pickle loop's dictionary access and
`CALL_PY_EXACT_ARGS`. XLang3's `push_frame` tested whether a function had only
positional-or-keyword parameters with no defaults by scanning its full
signature vector for every call.

The trial classified this shape once during lowering or IR decoding and put
the result on the `ir::Function`; the VM then used one byte-sized guard before
entering the existing argument-binding path. General signatures and all
binding/error semantics remained unchanged. The intended parallel to CPython
was the use of fixed code metadata to choose the exact-positional path.
The measurements show that this scan was not a meaningful cost in these
workloads.

## Raw evidence

- [Pickle candidate](data/signature-callshape-candidate-rigorous-20260930.json)
- [Pickle control](data/signature-callshape-control-rigorous-20260930.json)
- [DeltaBlue candidate, first order](data/signature-callshape-deltablue-candidate-rigorous-20260930.json)
- [DeltaBlue control, first order](data/signature-callshape-deltablue-control-rigorous-20260930.json)
- [DeltaBlue control, reverse order](data/signature-callshape-deltablue-control-repeat-rigorous-20260930.json)
- [DeltaBlue candidate, reverse order](data/signature-callshape-deltablue-candidate-repeat-rigorous-20260930.json)

The temporary candidate executable/runtime SHA-256 hashes were
`29436B4D28BF90D770CD2C9631AAC98E58EFD57334030F1447B5232CBE6B808D` and
`069F37B3279D028D93C04783364C01394E648941CE574B61FF44AD9105AC2098`.
