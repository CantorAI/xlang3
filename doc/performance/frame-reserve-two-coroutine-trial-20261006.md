# Initial VM-frame reserve trial (2026-10-06)

## Result

I reduced the fresh-evaluator `std::vector<VMFrame>` reserve from eight frames
to two, based on the large inline `VMFrame` footprint and repeated coroutine
resumes. The candidate did not produce a significant end-to-end improvement:

| Benchmark | Control fast | Candidate fast | Control rigorous | Candidate rigorous |
|---|---:|---:|---:|---:|
| `async_tree_none` | 4.45 s ± 0.06 s | 4.47 s ± 0.03 s | — | — |
| `coroutines` | 136 ms ± 13 ms | 133 ms ± 2 ms | 134 ms ± 8 ms | 134 ms ± 8 ms |

`pyperf compare_to` hid all fast-mode comparisons and the rigorous coroutine
comparison as statistically insignificant. The reserve change was reverted.
The coroutine means are compatible with no effect, and async-tree was slightly
slower on the candidate. No speedup is claimed.

The measurements show that the eager vector capacity alone is not a major
source of these coroutine benchmark gaps. The frame shell remains large, but
reducing the initial capacity did not improve measured runtime; another
optimization must avoid more of the per-resume work to move these benchmarks.

## Validation and build identity

The candidate passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and the complete fixture runner using
`C:\Python\Python314\python.exe` (CPython 3.14.7). The fixed Release pair has
been restored:

- Executable SHA-256: `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Runtime DLL SHA-256: `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`
- Candidate executable SHA-256: `5136443E7B807DF54C5E6036B5CAC09F6A88F0FA7317FD43E9E79E75003D4440`
- Candidate runtime DLL SHA-256: `54CEAB7B968BA28F7E807CFF656C2266173B42A90817C1C38F4369F09B3CB964`

All runs used pyperformance 1.14.0, Python 3.14.7, the same Windows
compatibility shim and dependency site. Fast runs used 20 measured values
across 10 workers; the rigorous coroutine pair used official rigorous mode.

## Raw evidence

- [Control fast JSON](data/frame-reserve-two-control-fast-20261006.json)
- [Candidate fast JSON](data/frame-reserve-two-candidate-fast-20261006.json)
- [Control rigorous JSON](data/frame-reserve-two-control-rigorous-20261006.json)
- [Candidate rigorous JSON](data/frame-reserve-two-candidate-rigorous-20261006.json)
