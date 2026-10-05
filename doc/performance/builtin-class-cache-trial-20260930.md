# Builtin class slot cache trial (2026-09-30)

## Result

Caching direct pointers to the runtime's builtin `int` and `float` map entries
did not improve the official pure-Python unpickle benchmark. The temporary
change was removed. It did not pass the performance acceptance check, so no
full Release regression gate was run for it.

| Order | Runtime | Mean ± standard deviation |
|---|---|---:|
| Candidate, then control | Candidate | 3.94 ± 0.16 ms |
| Candidate, then control | Control | 3.83 ± 0.23 ms |
| Control, then candidate | Control | 3.83 ± 0.14 ms |
| Control, then candidate | Candidate | 3.87 ± 0.25 ms |

`pyperf compare_to` reported the first candidate **1.03× slower**. The
reverse-order pair found no statistically significant difference. Both
pyperf runs warned that host variation left the result unstable. The data do
not support keeping the cache.

The unchanged workload was pyperformance 1.14.0's
`bm_pickle/run_benchmark.py --rigorous --pure-python --protocol 5 unpickle`,
using CPython 3.14.7 to launch pyperf 2.10.0 and the same Windows host. The
candidate passed the runtime-value C++ test and the complete Python fixture
runner before it was rejected on performance grounds.

## Why this was tested

The [CPython 3.14.7 VM comparison](cpython314-vm-comparison-20260930.md)
compares the same `pickle.py` implementation and warmed call site in both
runtimes. CPython specializes the dispatch dictionary subscription and exact
one-argument Python call. XLang3 already uses indexed local slots; its measured
cost is in general `GetItem`, call dispatch, and frame switching. The
instrumented profile also attributed about 8% of positive self-time to
`JumpIfFalse`.

For each instance truth test, XLang3 checked whether the class derives from
builtin `int` or `float`. The check looked up both builtin names in a string
keyed map before checking the cached class MRO. Reusing stable map slots would
remove those repeated name hashes while leaving subclass and special-method
semantics unchanged. The official result showed that this lookup was not a
useful target for this benchmark.

This comparison uses XLang3-only candidate/control times. CPython's saved
rigorous reference for this workload is about 0.164 ms; even the faster XLang3
control here is about 23× slower. The local trial does not change that gap.

## Raw evidence

- [Candidate, first order](data/builtin-class-cache-candidate-rigorous-20260930.json)
- [Control, first order](data/builtin-class-cache-control-rigorous-20260930.json)
- [Control, reverse order](data/builtin-class-cache-control-repeat-rigorous-20260930.json)
- [Candidate, reverse order](data/builtin-class-cache-candidate-repeat-rigorous-20260930.json)

The control executable/runtime SHA-256 hashes were
`0663E505177E3DA867F4C0DEC26A47F44209D9B6D5A76A2534F647A939F14E91` and
`66C2DDDE263525C5946BDCF5D46FD9EF81FB082E48EF3E02D245B397574A0919`.
The temporary candidate hashes were
`81D5FD9D5F8331FE831CE5DCDCD07AA8EDE5D62F971B38FF84C40F54D0B1B98E` and
`3021AD21E1864F9E612775F1BA1C2F7B5F970D3818991541275522C0FE1C2396`.
