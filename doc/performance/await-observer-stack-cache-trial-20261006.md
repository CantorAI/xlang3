# Cached observer state for inline coroutine Await (2026-10-06)

## Hypothesis

The exact-coroutine `Await` path checked whether any live VM frame had tracing
or monitoring hooks by scanning the whole current frame stack. Recursive
coroutines can execute this check at many stack depths. This trial replaced the
per-Await scan with a stack-wide flag initialized from a suspended continuation
and updated when frames are pushed. A monitoring-configuration generation
change conservatively disabled inlining for the rest of the active evaluator,
including when monitoring was enabled and disabled between awaits. Existing
runtime trace/profile guards and the per-child monitoring-code guard remained.

## Result

The source change was rejected. One fast pyperf pair measured:

| Runtime | `coroutines` mean |
|---|---:|
| Fixed Release control | 132 ± 3 ms |
| Observer-cache candidate | 133 ± 4 ms |

`pyperf compare_to` hid the difference as statistically insignificant. The
candidate also reported an instability warning, so there is no basis to claim
that the shorter check improves this workload. The original frame scan was
restored; no runtime change is retained.

## Validation and evidence

The candidate Release build passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and `tests/run_fixtures.py` using
`C:\Python\Python314\python.exe` (CPython 3.14.7). The fixed Release executable
and DLL were restored and their hashes match the saved control pair:

- Executable: `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548`
- Runtime DLL: `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D`

Candidate hashes:

- Executable: `98F02940A72D5366118E68FA773FA9B479E51F3A5DE8799FB3F1F28AEA6838EB`
- Runtime DLL: `7A1A0CA6D6B2707163D825FD50747E0362A393BB6F3A3CCC88493EC3D65392E7`

The test used pyperformance 1.14.0 `coroutines` in fast mode, with the Windows
compatibility shim and CPython 3.14.7 dependency site. Raw runs:

- [Control pyperf JSON](data/await-observer-stack-cache-control-fast-20261006.json)
- [Candidate pyperf JSON](data/await-observer-stack-cache-candidate-fast-20261006.json)

The evidence does not support this stack scan as a useful optimization target
for `coroutines`; the next candidate should address measured opcode execution
or ownership costs instead of another form of this observer check.
