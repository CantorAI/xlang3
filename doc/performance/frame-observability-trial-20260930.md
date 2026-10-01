# Frame observability guard trial (2026-09-30)

## Question

Does caching the current frame's tracing and line/instruction-monitoring state
remove enough work from XLang3's ordinary dispatch loop to improve
Python-heavy workloads?

## CPython 3.14.7 comparison

This check used CPython's tagged implementation source, not an assumption
about what a fast interpreter should do. In CPython 3.14.7, the ordinary
`RESUME` path includes instrumentation setup at resume points, while
`CALL` and backward jumps include `_CHECK_PERIODIC` for pending evaluator
events. Per-op tracing and monitoring are represented by instrumented code
paths. See [`bytecodes.c` at `v3.14.7`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L153-L170)
and its [`CALL` and back-edge macros`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2923-L2941).

The same file shows the type of specialization behind CPython's hot paths:
[`LOAD_GLOBAL_MODULE`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L1797-L1814)
guards the globals dictionary version and reads a cached entry index, and
[`CALL_PY_EXACT_ARGS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L4041-L4050)
combines guards, argument transfer, and frame entry. This is the relevant
design comparison: XLang3's IR indexes already avoid repeatedly resolving
source names, but the measured cost remains in generic dispatch, calls,
frame switches, and dynamic attribute semantics. The wider benchmark and
hot-operation evidence is recorded in the
[DeltaBlue CPython comparison](deltablue-cpython314-implementation-analysis-20260930.md).

## Candidate and result

The candidate computed a frame-local tracing/monitoring-active bit on frame
entry, refreshed it after callbacks and calls that can change monitoring, and
used the cached bit at each opcode. This targeted repeated frame-field reads
and hook checks while preserving the active tracing path. The full Python
fixture runner passed on the candidate.

The fast screen looked slightly favorable for DeltaBlue, but both fast runs
reported instability. The paired rigorous samples were also too noisy to
show an effect, and `pyperf compare_to -v` marked both benchmarks
statistically insignificant:

| Benchmark | Parent | Candidate | Decision |
|---|---:|---:|---|
| `deltablue` | 40.6 ± 2.4 ms | 41.4 ± 4.0 ms | No significant change |
| `unpickle_pure_python` | 3.50 ± 0.44 ms | 3.42 ± 0.36 ms | No significant change |

The candidate was rejected and its source edits were reverted. We should not
claim a speedup or retain a per-frame cache based on this result. The
experiment does not explain the large CPython gap; generic VM dispatch and
call/frame costs remain the dominant measured targets.

## Raw evidence

- [DeltaBlue parent rigorous data](data/frame-observability-parent-rigorous-20260930.json)
- [DeltaBlue candidate rigorous data](data/frame-observability-candidate-rigorous-20260930.json)
- [Parent rigorous log](data/frame-observability-parent-rigorous-20260930.log)
- [Candidate rigorous log](data/frame-observability-candidate-rigorous-20260930.log)
- [Parent fast data](data/frame-observability-parent-fast-20260930.json)
- [Candidate fast data](data/frame-observability-candidate-fast-20260930.json)
