# Keep native `any()` inside one generator VM entry (2026-10-06)

## Change

When native `any()` consumes a synchronous generator, the VM can continue past
false yields in the same interpreter invocation. Those values cannot change
`any()`'s result, and the next operation on a suspended generator receives
`None`, so the fast path writes that send value directly into the generator's
yield destination. A truthy yield and generator completion still return through
the normal path. The optimization is disabled when tracing, profiling,
monitoring, debugging, performance counters, or a suspended exception handler
can observe the yield. A code comment beside the yield handler records this
invariant and the fallback conditions.

This is an XLang3 VM optimization for a CPython-native builtin. It does not
replace or modify the pure-Python `comprehensions` benchmark or standard
library code.

## Result

The unchanged pyperformance 1.14.0 `comprehensions` benchmark was measured
with CPython **3.14.7** at `C:\Python\Python314` as the harness and dependency
runtime. Three rigorous candidate runs compared with two fixed-Release
controls gave a small, somewhat noisy improvement. `pyperf compare_to` found
two candidate runs faster by 3–4% and one run statistically indistinguishable.
Worker outliers made the mean and standard deviation unstable, so the size of
the gain is not precise.

| Build | Mean per loop | Median per loop | Relative to control |
| --- | ---: | ---: | ---: |
| Fixed Release control 1 | 171 µs ± 17 µs | 168 µs | — |
| Fixed Release control 2 | 170 µs ± 12 µs | 168 µs | — |
| Candidate 1 | 165 µs ± 16 µs | 162 µs | 1.03× faster |
| Candidate 2 | 167 µs ± 15 µs | 164 µs | not significant |
| Candidate 3 | 164 µs ± 10 µs | 161 µs | 1.04× faster |

Against the same-host saved CPython 3.14.7 result of **14.22 µs**, the
candidate median is about **0.088× CPython speed** (roughly **11.4× slower**).
This does not close the comprehension gap. The separate official `generators`
regression check was **318 ms ± 18 ms** on control and **320 ms ± 18 ms** on
candidate; `pyperf compare_to` hid the difference as insignificant.

## Validation and build identity

The focused fixture checks false and truthy yields, full exhaustion, a partly
consumed generator, and a generator suspended inside `try/finally`. The
complete fixture runner passed under CPython 3.14.7. `xlang3_runtime_value_tests`
and `xlang3_interpreter_tests` passed. CTest passed **54 of 55** tests; the
remaining Visual Studio debugpy launch smoke test failed its existing
configuration assertion that the VS profile invoke `xlang3.exe` directly. No
debugpy launch configuration was changed by this trial.

| Binary | SHA-256 |
| --- | --- |
| Control executable | `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548` |
| Control runtime DLL | `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D` |
| Candidate executable | `3928C09A3BC954414A8F6F95AF125E229B4DD5977D25685B42D53E570CFB1684` |
| Candidate runtime DLL | `4066CF48EDE4C5E98869920CA1954C2F531967B5FA12A49BFDF87F2CCD84887D` |

Raw pyperf data:

- Comprehensions: [control 1](data/pyperformance-any-consume-control-rigorous-20261006.json), [control 2](data/pyperformance-any-consume-control-r2-rigorous-20261006.json), [candidate 1](data/pyperformance-any-consume-candidate-r1-rigorous-20261006.json), [candidate 2](data/pyperformance-any-consume-candidate-r2-rigorous-20261006.json), [candidate 3](data/pyperformance-any-consume-candidate-r3-rigorous-20261006.json).
- Generator regression: [control](data/pyperformance-any-consume-generators-control-rigorous-20261006.json), [candidate](data/pyperformance-any-consume-generators-candidate-rigorous-20261006.json).

The full pyperformance objective remains open; this is a small targeted
improvement, not evidence that XLang3 has caught CPython overall.
