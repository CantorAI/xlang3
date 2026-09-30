# Native VM timing diagnosis (2026-09-30)

The native profile points to frequent opcode execution and frame transitions,
rather than function-invocation preparation alone. In the unchanged official
pure-Python unpickle workload, item access accounts for about 14% of measured
positive self-time, calls and method calls together 23%, and frame transitions
11%. Invocation setup contributes below 1%. These are diagnostic shares from
an instrumented build, **not speed gains or pyperf scores**.

CPython's `pickle.py` and other pure-Python library code remain Python. This
probe measures XLang3's shared interpreter; it does not replace a library
algorithm with C++ or execute the workload in CPython.

## Method and limits

The source revision was `1ece2e1`. The diagnostic build adds scoped native
timers around VM invocations, frame switches, loop control, and individual IR
opcodes. A nested scope subtracts its elapsed time and timer bookkeeping from
its parent, preventing recursive interpreter calls from being counted twice
in self-time. The source is
[`native_vm_opcode_timer.h`](../../benchmarks/diagnostics/native_vm_opcode_timer.h);
the [temporary engine patch](data/native-vm-opcode-timing-probe-20260930.patch)
records the placement of each scope. Ordinary Release builds do not include
the probe header. The diagnostic build enables reporting only when
`XLANG3_VM_OPCODE_TIMING=1` is set.

The unchanged pyperformance 1.14.0 `bm_pickle/run_benchmark.py` ran with
`--pure-python --protocol 5 unpickle`. The
[direct workload runner](../../benchmarks/diagnostics/run_pyperformance_workload_cli_direct.py)
substitutes pyperf's worker runner while preserving the benchmark body and
arguments. After one import-cache warmup, separate processes ran 1, 21, and
41 outer workload loops. The benchmark contains 20 inner units per outer
loop. The per-outer-loop numbers here must not be compared directly with
pyperf's normalized per-inner-unit times.

Subtracting the 1-loop process from the longer processes removes most common
startup/import work. It does not eliminate scheduling noise. The
[summarizer](../../benchmarks/diagnostics/summarize_vm_opcode_timings.py) retains
all negative raw deltas in its CSV/JSON output. Shares below use positive
self-time deltas whose call-count deltas are also positive. Allocation and
reference-count diagnostic counters were inactive.

Clock pairs averaged 14 ns. Clock reads, scope bookkeeping, and aggregate
atomics perturb execution, especially cheap scalar opcodes. This profile
locates expensive areas but cannot establish their precise share in the
ordinary build, an optimization's benefit, or a CPython comparison. All real
performance claims require the preserved ordinary Release binaries and the
official benchmark.

## Workload-subtracted attribution

| Scope | 21 minus 1 share | 41 minus 1 share | Calls per extra outer loop | Self ms per outer loop, 41 minus 1 | Self ns per call, 41 minus 1 |
|---|---:|---:|---:|---:|---:|
| VM loop control | 19.06% | 18.75% | 668,187 | 24.808 | 37.1 |
| `GetItem` | 13.82% | 13.84% | 37,720 | 18.305 | 485.3 |
| `Call` | 11.60% | 12.13% | 36,260 | 16.051 | 442.7 |
| VM frame switch | 10.96% | 10.87% | 62,500 | 14.379 | 230.1 |
| `CallMethod` | 10.85% | 10.86% | 36,560 | 14.366 | 393.0 |
| `JumpIfFalse` | 7.80% | 7.70% | 61,560 | 10.189 | 165.5 |
| `LoadLocalAttr` | 4.14% | 4.14% | 49,340 | 5.477 | 111.0 |
| `ReturnLocal` | 3.58% | 3.52% | 20,180 | 4.656 | 230.7 |
| `ReturnConst` | 2.24% | 2.25% | 10,680 | 2.979 | 278.9 |
| `LoadLocal` | 2.25% | 2.18% | 106,521 | 2.880 | 27.0 |
| `LoadGlobalLocal` | 1.42% | 1.42% | 10,760 | 1.875 | 174.3 |
| `Len` | 1.04% | 1.03% | 24,000 | 1.361 | 56.7 |
| VM invocation | 0.94% | 0.95% | 20 | 1.255 | 62,728.8 |

The two differences agree closely on attribution and call counts. The raw
direct workload elapsed times were 0.1742014 s for 1 loop, 3.5879097 s for
21 loops, and 7.0690305 s for 41 loops. The setup-subtracted instrumented
workload costs approximately 171–172 ms per outer loop. This is deliberately
not presented as a benchmark score.

```text
Measured positive native self-time delta, 41 minus 1
VM loop control  18.75% |███████████████████
GetItem          13.84% |██████████████
Call             12.13% |████████████
VM frame switch  10.87% |███████████
CallMethod       10.86% |███████████
JumpIfFalse       7.70% |████████
LoadLocalAttr     4.14% |████
VM invocation     0.95% |█
```

Raw processes: [1 loop](data/unpickle-native-vm-timing-1loop-20260930.txt),
[21 loops](data/unpickle-native-vm-timing-21loops-20260930.txt),
[41 loops](data/unpickle-native-vm-timing-41loops-20260930.txt).
Complete derived rows: [20-loop CSV](data/unpickle-native-vm-timing-delta20-20260930.csv),
[20-loop JSON](data/unpickle-native-vm-timing-delta20-20260930.json),
[40-loop CSV](data/unpickle-native-vm-timing-delta40-20260930.csv),
[40-loop JSON](data/unpickle-native-vm-timing-delta40-20260930.json).
JSON includes raw values, deltas, loop counts, clock calibration, and hashes
of both profiles and the IR enum.

## Next investigation

The previous prepared-metadata trial addressed invocation preparation and
did not materially close the gap. This native profile supports investigating
opcode paths instead. `GetItem` already specializes bytes/integer access;
the prior exact-dict integer shortcut was also neutral. Frame-view publication
already updates incrementally on pushes and pops. Those existing optimizations
should not be rediscovered or rewritten without evidence.

One concrete remaining cost is ordinary instance truth: it looks up numeric
builtins and queries inheritance before resolving `__bool__` or `__len__`.
The inheritance query currently materializes a pointer vector even when the
class's Value MRO is cached. Reading that cache directly is a generic runtime
candidate. The official unpickle benchmark and the complete fixed Release
gate must determine whether it is worth retaining.

The goal is still unmet. Before this profile, the ordinary-build rigorous comparison was
4.3614 ms for XLang3 against the earlier same-day CPython 3.14.7 reference of
0.1616 ms: approximately **26.99× longer**, or **0.037× speed with CPython
= 1.0×**. See the [previous trial record](vm-prepared-metadata-trial-20260930.md)
for those raw official results. The CPython reference was not rerun for this
diagnostic profile. The [MRO follow-up](subclass-mro-reuse-20260930.md) records
the subsequent generic runtime change, official comparisons, and validation.

## Correctness and build identities

The isolated diagnostic build passed the entire fixture runner with probes
disabled: 300 core fixtures, 11 compatibility sections, and three expected
failures, exit 0. Four focused fixtures passed with probes enabled:
`runtime_specialization_semantics`, `code_traceback_model`,
`vm_borrowed_local_overwrite`, and `pickle_module`.

An initial isolated run lacked the `_ssl` native package; copying the normal
Release `modules` directory resolved that environment issue. An active-probe
full-suite attempt then failed `sys_command_path` because it expects empty
child stderr, while the probe intentionally reports there. The opt-in flag
allows the complete suite to run normally. A full suite with active reporting
is not claimed.

| Binary | Ordinary / preserved | Diagnostic |
|---|---|---|
| `xlang3.exe` | `2E221C1EBCA6E5D3560D530A08BAA2384FBFD49B9D0FB2250D6CD80D86410D27` | `7EB67A0454BFB90A35DAE0E38B3167AB18A58DAC97703926E6B24B2A3D6082D9` |
| `xlang3_runtime.dll` | `5A4A9A236C7426E1D2BD5430C01DB97487B2E82D33719AC2D9A2A9A28C994CC6` | `15E0AB0189471BA0E78CB5B5C3D7DF00A9AC73BA6515A8FF0C80610A82196577` |

The temporary instrumentation was removed from engine source and both ordinary
binaries were restored and hash-checked before any optimization comparison.
The diagnostic header and patch are kept for reproduction, not compiled into
the normal runtime.

## Reproduction

Preserve the ordinary Release executable and runtime first. Use the supplied
diagnostic header with engine source from revision `1ece2e1`, apply the saved
patch with `git apply --unidiff-zero`, build Release,
and copy the resulting executable, runtime, and `modules` directory to a
separate diagnostic directory. Restore the engine source and ordinary binary
pair before comparing performance.

Set `PYTHONPATH` to the installed benchmark dependency environment's
`Lib/site-packages` and use an isolated `PYTHONPYCACHEPREFIX`. For each of 1,
21, and 41 loops:

```powershell
$env:XLANG3_VM_OPCODE_TIMING = '1'
$env:PYPERF_DIRECT_LOOPS = '<loops>'
<diagnostic>/xlang3.exe `
  benchmarks/diagnostics/run_pyperformance_workload_cli_direct.py `
  C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py `
  --pure-python --protocol 5 unpickle *> <profile.txt>
```

Derive the complete table using the IR enum from the profiled revision:

```powershell
C:/Python/Python314/python.exe benchmarks/diagnostics/summarize_vm_opcode_timings.py `
  <41loops.txt> --baseline <1loop.txt> `
  --ir src/internal/xlang3/ir.h --csv <delta40.csv> --json <delta40.json>
```

Unset `XLANG3_VM_OPCODE_TIMING` when running ordinary correctness or official
performance comparisons. Never compare this diagnostic binary's elapsed
times with CPython or an uninstrumented Release build as a performance result.
