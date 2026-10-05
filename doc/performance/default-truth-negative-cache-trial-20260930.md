# Default instance truth cache trial (2026-09-30)

## What was tested

The candidate cached the default `True` result for an ordinary instance after
checking that its class had neither `__bool__` nor `__len__`, and was not a
numeric subclass. It kept native per-instance truth callbacks ahead of the
cache and guarded cached answers with the class version and a process-wide
builtin-registration epoch. The aim was to skip repeated numeric-base and
special-method lookups for classes with default truth.

The implementation changed the generic XLang3 runtime and `ClassObject`. It
did not move any pure-Python library code into C++.

## Result: rejected

The official `deltablue` benchmark did not improve. Two rigorous pairs ran in
opposite runtime orders with pyperformance 1.14.0:

| Order | Parent | Candidate |
| --- | ---: | ---: |
| Candidate, then parent | 57.9 ± 1.3 ms | 59.0 ± 2.9 ms |
| Parent, then candidate | 58.7 ± 1.5 ms | 58.4 ± 1.1 ms |

Pooling the two runs gives **58.3 ± 1.5 ms** for the parent and **58.7 ±
2.2 ms** for the candidate. `pyperf compare_to` reports the candidate about
**1% slower** (`t=-2.35`). The individual runs carry pyperf's sample-instability
warning, but neither order shows a useful gain, so the cache was removed.

Against the saved CPython 3.14.7 full-fast result of 2.485 ± 0.04 ms, the
candidate still takes about **23.6× as long**. That reference is from the
September 28 full-suite run; this trial did not rerun all 97 benchmarks.

The candidate passed the full Python fixture runner and the C++ interpreter
tests, including checks for changing inherited truth hooks, replacing
`__bases__`, descriptor behavior, and sharing a class between runtimes with
different builtin registries. A fixed-baseline performance gate was not run
because the official target benchmark failed to show a benefit.

## Raw evidence

- Parent rigorous samples, [first](data/deltablue-default-truth-parent-rigorous-20260930.json) and [reverse-order repeat](data/deltablue-default-truth-parent-repeat-rigorous-20260930.json)
- Candidate rigorous samples, [first](data/deltablue-default-truth-candidate-rigorous-20260930.json) and [reverse-order repeat](data/deltablue-default-truth-candidate-repeat-rigorous-20260930.json)
- Pooled pyperf inputs, [parent](data/deltablue-default-truth-parent-merged-rigorous-20260930.json) and [candidate](data/deltablue-default-truth-candidate-merged-rigorous-20260930.json)
- CPython reference: [full pyperformance 1.14.0 run](data/pyperformance-cpython314-full-fast-20260928.json)

The candidate executable and runtime DLL hashes were
`E881E42BDC365BD0F8DA2099002A96398B2814A58C39BEA83813A5502E62DEA1` and
`8EE73A9CA1811911DF4ADD263C9558C38AAA11A34EE1A34AEE0F495E2FFFB9A4`.
The parent executable and runtime DLL hashes were
`C6C554AA507EDB9B58308C4E2C933F2CB0F6653711E8C0FE0D1FA5DE6CAB182B` and
`9D683BFBD3664292D5DDD1C5092A8FF00C4316F9FCE939CC67AEA8B2C713F81D`.
