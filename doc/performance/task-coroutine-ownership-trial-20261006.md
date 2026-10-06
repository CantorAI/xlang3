# Task coroutine ownership-transfer trial (2026-10-06)

## Result

During native `asyncio.Task` stepping, the candidate transferred the Task's
coroutine owner into the active native call and left a borrowed alias in the
Task field. An RAII guard restored the owning field unless reentrant
`Task.__init__` replaced it. This removed the extra `Value` retain/release
pair while keeping the coroutine alive across reentrant replacement.

The candidate passed the full fixture runner, `xlang3_runtime_value_tests`,
and `xlang3_interpreter_tests`. The official pyperformance 1.14.0
`async_tree_none` A/B was neutral: both builds measured **4.46 s**, and
`pyperf compare_to` hid the difference as insignificant. The code change was
reverted; no speedup is claimed.

| Build | `async_tree_none` mean ± standard deviation | Relative speed |
|---|---:|---:|
| Fixed Release control | 4.46 s ± 0.03 s | 1.000× |
| Borrowed-owner candidate | 4.46 s ± 0.04 s | 1.000× (not significant) |

Each run had 20 measured values across 10 worker processes, one warmup per
worker, and one loop per value. Both used Python **3.14.7**, pyperformance
**1.14.0**, the Windows compatibility shim, and the same dependency site.
Against the saved CPython 3.14.7 result of 227.4 ms, the candidate remained
about **0.051× CPython speed** (19.6× slower).

This rejects one Task-step retain/release pair as a meaningful async-tree
optimization. The broader native sample's `Value` release samples therefore
need to be traced to other VM/runtime paths before another ownership shortcut
is considered.

## Evidence and build identities

- [Control pyperf JSON](data/task-coroutine-ownership-control-fast-20261006.json)
- [Candidate pyperf JSON](data/task-coroutine-ownership-candidate-fast-20261006.json)

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Fixed Release control | `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548` | `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D` |
| Borrowed-owner candidate | `F1B20805968B9E3FD6E25871E90F4C240264FEFD17074B97FB77B7246CAD98A6` | `DD07215CA16B0E4BCF3E27678582B3810DAC7F03615F20BFBCB57A1991B26F96` |

The unchanged candidate was built from the fixed control source with only the
Task coroutine ownership transfer. Its Release artifacts were copied to an
isolated trial directory. The fixed control executable and runtime were
restored and hash-checked after the trial. Reproduce with
`benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py`,
`--benchmarks async_tree --mode fast --case-timeout-override async_tree=600`,
the runtime paths represented by the table, and the CPython 3.14.7 dependency
site.
