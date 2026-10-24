# Async-tree Task resume timing — 2026-10-10

## Finding

The hypothesis that constructing a fresh `Interpreter` at each native Task
resume is a large part of the async-tree gap is not supported. In one run of
the official `NoneAsyncTree` body, the native Task-step implementation took
2.925 s across 65,320 calls. Its direct `generator_send` path took 2.226 s
across 66,330 calls. The part of `generator_send` outside the root coroutine's
VM execution was 18.5 ms total, about 0.63% of the Task-step time and 0.45% of
the measured 4.154 s process run. A continuation optimization aimed at
removing this interpreter-entry/setup work has too little measured headroom to
justify its semantic risk.

The root coroutine's `run_function` scopes accounted for 2.205 s total. Those
scopes include the coroutine body and its nested Python calls, so this is not
an exclusive VM-loop attribution. The next useful work should target the
measured work inside VM execution and Task yield handling, not the `Interpreter`
object construction at the boundary.

## Measurement

The probe added opt-in nested timers around `step_impl`, `generator_send`, and
`Interpreter::run_function` only when called with a generator. It executed
`NoneAsyncTree().run()` from the installed pyperformance 1.14.0
`bm_async_tree/run_benchmark.py`; this is a diagnostic body run, not a
pyperformance score. A matching run with the instrumentation present but its
timer flag disabled took 4.045 s; the timer-enabled run took 4.154 s (2.7%
slower). The nested timer's `self_ns` therefore provides directional
attribution, while the elapsed values are not benchmark results.

The counts and timer totals were:

| Scope | Calls | Self time | Total time |
| --- | ---: | ---: | ---: |
| `step_impl` | 65,320 | 699.4 ms | 2.925 s |
| `generator_send` | 66,330 | 18.5 ms | 2.226 s |
| Root coroutine `run_function(generator != nullptr)` | 66,330 | 2.205 s | 2.205 s |

The `step_impl` and `generator_send` scopes share the same timer state in the
Release runtime and nest correctly. The run-function scope was restricted to
the root generator frame so it did not add timer calls to the hundreds of
thousands of ordinary Python function frames. The root scope includes nested
Python work and is intentionally treated as inclusive.

With timer code removed, a current-build `--perf-counters` run counted
1,782,650 `LoadLocalAttr`, 1,630,932 `LoadLocalPair`, 1,393,169
`JumpIfFalse`, 1,197,827 `CallMethod`, 1,049,111 `StoreLocal`, and 938,153
`StoreAttr` dispatches in this same body. Counts show that execution performs
large volumes of VM work; they are not time attribution. They make a shared VM
dispatch/operation optimization the next target to profile, rather than
another native Task callback cache or a resume-continuation optimization.

## Evidence

- Timer-enabled body output: [`async-tree-task-resume-timing-sampled-20261010.log`](data/async-tree-task-resume-timing-sampled-20261010.log) and [metadata](data/async-tree-task-resume-timing-sampled-20261010.json).
- Timer-disabled body output: [`async-tree-task-resume-untimed-20261010.log`](data/async-tree-task-resume-untimed-20261010.log) and [metadata](data/async-tree-task-resume-untimed-20261010.json).
- Diagnostic Release executable hash: `5C12A6763E28CF94F7B59A11516EF5FE27EEDCC0A946D3B5369D48B3D5A3071A`.
- Pre-instrumentation fixed Release executable/runtime hashes: `E684D30F138B77C67B8D1A5BEEB636BBD42907D66D1C675FBF023ECDE26FB45F` / `DC09E602E3CAC4A6335F2D52568D9B54870B7E389D18B3B50F8F26741BB2D33C`.
- Current Release opcode counts: [`async-tree-none-current-release-perf-counters-20261010.log`](data/async-tree-none-current-release-perf-counters-20261010.log) and [decoded opcode totals](data/async-tree-none-current-release-opcode-counts-20261010.txt).
- Fixture run after timer removal: [Python 3.14.7 status](data/task-resume-instrumentation-revert-fixtures-status-20261010.json).
- The pre-instrumentation Release directory is preserved at `build-repro/baseline-current-fixed-release-20261010`; benchmarks continue to use `build-repro/main-verify-20261006/Release`.

The timing hooks were temporary diagnostic code. They must be removed and the
fixed Release directory rebuilt before any further performance benchmark. They
have now been removed; the existing build command rebuilt the unchanged fixed
run path, and the full fixture runner exited 0 under `C:\Python\Python314`.
That working-tree Release executable has SHA-256
`9579A6351E8A0BD15FCA401DDC80E91D7238C21EBAEFCCD66B0BB0BA4279F4A6` and its
runtime DLL has SHA-256
`259BC7976F5CAA9FA6360876C0E09ADDA829859A2A6CC2708BBEB4E13011DD28`.
The saved baseline copy has the older `E684D30F...` executable hash; the
current source had other pending runtime edits that were not in that binary.
The full 97-definition suite still needs to run against the current working
tree before its aggregate score can be stated.
