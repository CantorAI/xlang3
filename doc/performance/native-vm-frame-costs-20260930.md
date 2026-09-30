# Native call/return cost diagnosis (2026-09-30)

Cache cleanup is the largest separately measured frame subcost in the
unchanged official pure-Python unpickle body. Two setup-subtracted profiles
attribute 5.52–5.64% of positive measured self-time to cache cleanup,
3.71–3.91% to frame reset, and 2.50–2.53% to value cleanup. Most time remains
in interpreter execution outside these subscopes. These diagnostic shares
are not speedups or ordinary Release benchmark scores.

This follows the rejected [cache storage trials](vm-cache-storage-trials-20260930.md).
Their added cache indirection did not improve the benchmark, so this probe
measures existing frame work before proposing another representation change.
CPython's pure-Python libraries remain Python.

## Probe and limits

The probe uses the [native scoped timer](../../benchmarks/diagnostics/native_vm_opcode_timer.h)
at VM invocation, call preparation, frame reset, cache cleanup, value cleanup,
published frame-view refresh, and current-frame identity publication. It
does not read a timer on every opcode. The
[temporary source patch](data/native-vm-frame-costs-probe-20260930.patch)
applies to `e4d9e90` with `git apply --unidiff-zero`; use the diagnostic header
from this report's revision. Ordinary engine source does not include it.

Nested scopes are subtracted from their parents. Call preparation excludes
frame reset and any recursive interpreter execution; the invocation row is
the residual interpreter work after excluding the six other subscopes.
`clear_for_pop` clears globals and a continuation value before the measured
cache scope; that small prefix stays in the residual invocation row. These
rows are self-time, not additive inclusive totals.

The unchanged pyperformance 1.14.0 `bm_pickle` script runs with
`--pure-python --protocol 5 unpickle` through the
[direct diagnostic runner](../../benchmarks/diagnostics/run_pyperformance_workload_cli_direct.py).
It preserves the benchmark body and its 20 inner units per outer loop but
bypasses pyperf worker calibration. After a warmup process, fresh processes
execute 1, 21, and 41 outer loops with `XLANG3_VM_OPCODE_TIMING=1`.
Subtracting the 1-loop process removes most import/startup cost. Scheduling
and clock-probe overhead remain. Clock pairs averaged 14 ns.

The 21-loop and 41-loop processes report workload times of 1.97181 s and
3.80464 s respectively. These are instrumented, unnormalized diagnostics;
do not compare them with CPython or ordinary pyperf scores.

## Attribution

| Scope | 21 minus 1 share | 41 minus 1 share | Calls per extra outer loop | Self ns per call, 41 minus 1 |
|---|---:|---:|---:|---:|
| Residual interpreter invocation | 81.73% | 81.74% | 20 | 3,489,990 |
| Cache cleanup | 5.52% | 5.64% | 31,260 | 154.0 |
| Frame reset | 3.91% | 3.71% | 31,220 | 101.4 |
| Call preparation, excluding reset | 2.56% | 2.62% | 31,240 | 71.7 |
| Value cleanup | 2.50% | 2.53% | 31,260 | 69.1 |
| Frame-view refresh | 2.06% | 2.03% | 62,500 | 27.8 |
| Current-frame publication | 1.72% | 1.73% | 62,500 | 23.6 |

Raw processes: [1 loop](data/unpickle-native-frame-costs-1loop-20260930.txt),
[21 loops](data/unpickle-native-frame-costs-21loop-20260930.txt),
[41 loops](data/unpickle-native-frame-costs-41loop-20260930.txt).
Complete derived data: [20-loop CSV](data/unpickle-native-frame-costs-delta20-20260930.csv),
[20-loop JSON](data/unpickle-native-frame-costs-delta20-20260930.json),
[40-loop CSV](data/unpickle-native-frame-costs-delta40-20260930.csv),
[40-loop JSON](data/unpickle-native-frame-costs-delta40-20260930.json).
The summarizer preserves raw deltas and hashes its source profiles and IR.

## Correctness and identities

The diagnostic Release build passed all 302 core fixtures, 11 compatibility
sections, and three expected failures with probes disabled. It also completed
the unchanged official body in every active-probe process. Active-probe full
fixture validation is not claimed because deliberate stderr reporting changes
child-process output assumptions.

| Binary | Accepted / restored | Diagnostic |
|---|---|---|
| `xlang3.exe` | `243F6A336E317FF60942217BFEA77632182B917B7A4DCCD72E7FD333356CC7C0` | `C33A82599F29F9C72DA310902AE2D028049E0DED6B77CC03E20D1DA9EFC4344B` |
| `xlang3_runtime.dll` | `2C371933BDE80E66CE85122C3516278BCDD2674DE209B281A5DC9275C17FBEAA` | `C85F49E16D2397131C4185F823F00CC21D793E14C3BBCB367A2CA045FA5C796B` |

The source patch was removed and the accepted executable/runtime pair restored
before the next ordinary optimization build. No timing probe is part of a
retained engine change. A proposed cache-cleanup improvement must pass full
correctness, both complete fixed/parent Release gates, and the official
benchmark before being retained.

The [cleanup follow-up](vm-cache-domain-cleanup-20260930.md) records the later
generic runtime change, both rigorous pairs, fresh CPython comparison, complete
gates, and correctness evidence. The ordinary runtime excludes this probe.
