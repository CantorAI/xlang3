# DeltaBlue: XLang3 versus CPython 3.14.7 implementation analysis

This report compares the same official pyperformance 1.14.0 `deltablue`
workload on XLang3 and CPython 3.14.7. It uses CPython's implementation and
the warmed benchmark bytecode to explain why the classmethod comparison
specialization helped only a little, and to identify the larger shared-VM
targets. `run_benchmark.py` and the standard-library benchmark code remain
Python in XLang3.

## Result

The classmethod optimization removes binding and frame work for two exact
`Strength` methods, but the whole benchmark improves only about 2%:

| Runtime or change | Mean ± standard deviation | Speed with CPython = 1.0× |
|---|---:|---:|
| CPython 3.14.7 | 2.73 ± 0.21 ms | 1.000× |
| XLang3 parent | 59.9 ± 1.4 ms | 0.046× (21.94× slower) |
| XLang3 classmethod fast path | 58.5 ± 1.6 ms | 0.047× (21.43× slower) |

The two XLang3 samples were pooled from two rigorous runs each, alternating
execution order. `pyperf compare_to` reports the candidate is **1.02× faster**
than its parent (`t=10.24`). CPython and the candidate used the same official
benchmark body and rigorous pyperf settings. This is a real, small gain; it
does not close the CPython gap.

Elapsed time, with shorter bars indicating faster execution:

```text
CPython 3.14.7             2.73 ms |█
XLang3 parent             59.9  ms |██████████████████████████████████████████████████
XLang3 candidate          58.5  ms |█████████████████████████████████████████████████
```

The [parent samples](data/classmethod-attr-int-compare-parent-merged-rigorous-20260930.json),
[candidate samples](data/classmethod-attr-int-compare-candidate-merged-rigorous-20260930.json),
[CPython samples](data/classmethod-attr-int-compare-cpython314-merged-rigorous-20260930.json),
and [fixed-baseline gate report](data/classmethod-attr-int-compare-fixed-baseline-20260930.json)
preserve the full pyperf data and executable/runtime identities.

## What CPython does for these methods

The benchmark defines `Strength.stronger` and `Strength.weaker` as classmethods
that load `strength` from each operand and compare the two values. In a warmed
CPython 3.14.7 process, the method bodies use adaptive
`LOAD_ATTR_INSTANCE_VALUE` operations followed by `COMPARE_OP_INT`. The
specialized attribute operation guards the receiver type and managed-dict
layout, then reads the cached value position. The integer comparison guards
exact integer operands and compares their immediate values. CPython's
specialization and opcode definitions are in [`Python/specialize.c`](https://github.com/python/cpython/blob/v3.14.7/Python/specialize.c)
and [`Python/bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c).

Classmethod access itself is not the source of a CPython win: CPython's class
attribute specialization does not specialize the Python classmethod descriptor
used here, and its descriptor getter creates a bound method. See
[`Objects/funcobject.c`](https://github.com/python/cpython/blob/v3.14.7/Objects/funcobject.c).
The XLang3 fast path avoids that binding and skips the tiny Python frame for
exact supported inputs, but the instrumented DeltaBlue profile counted only
708 `stronger` and 1,205 `weaker` calls out of 51,421 benchmark-function calls.
The inference from those counts is that this specialization can improve only
a small slice of the run; it cannot explain the roughly 21× whole-test gap.

## Where the larger XLang3 cost sits

The matching XLang3 call profile has the same overall shape of work: `output`
was called 14,054 times, `execute` 12,903, and `input` 11,707. Those are many
small Python calls. The XLang3 VM profile for a separate pure-Python
`unpickle` workload attributes about 19% of positive instrumented self-time to
loop control, 23% to `Call` plus `CallMethod`, and 11% to frame transitions.
That profile is not a DeltaBlue timing attribution, but it independently
shows that shared interpreter dispatch and Python call/frame execution are
important optimization targets.

The IR already stores identifiers for attribute names. That avoids repeatedly
parsing source names; it does not make every object read a direct field load.
For ordinary instance attributes, [`load_local_attr`](../../src/executor/xlang_vm/ops/xlang_vm_ops_attr.h)
still passes through the general attribute path. Its warmed cache in
[`xlang_vm_load_attr_cached`](../../src/executor/xlang_vm/xlang_vm_attr.cpp)
keeps an attribute-vector index, but validates that the value at that index
still has the requested string name. CPython's managed instance dictionaries
share a class key layout and its adaptive opcode guards that layout before
loading by offset. This difference matters when thousands of repeated reads
and method calls run inside the interpreter.

The evidence points to shared VM execution costs. Any specialized operation
must preserve descriptors, dynamic instance attributes, subclass overrides,
hooks, tracing, and monitoring. A follow-up DeltaBlue-specific native timing
profile below measures the loop, frame transitions, and hot IR operations in
this workload rather than inferring their shares from unpickle.

## Validation and limits

The candidate passed the complete 11-case fixed-baseline Release gate and the
Python-driven fixture runner. CTest passed 52 of 53 tests; the remaining CLI
fixture failure is the pre-existing Python 3.14 `typing.Literal` unhashable
case, reproduced with the saved parent runtime as well. The standalone full
fixture runner succeeds. The pyperformance result above is the specific
`deltablue` target; it is not a new full-suite rerun.

The optimization and its semantic guards are in [`xlang_vm_inline_call.h`](../../src/executor/xlang_vm/xlang_vm_inline_call.h)
and [`xlang_vm_ops_call.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_call.h).
The fixture exercises the specialized methods and its generic fallbacks in
[`classmethod_attr_int_compare.py`](../../tests/fixtures/core/classmethod_attr_int_compare.py).

## Follow-up: cache lifetime across repeated calls

The source comparison identified a broader difference than classmethod
binding: CPython retains adaptive attribute and call guards on each bytecode
site, while XLang3 had been discarding selected caches at frame return to
release owning Values. A follow-up now keeps only non-owning attribute and
class-owned method guards warm across calls. Two opposite-order rigorous
pyperf pairs measure **62.8 ms to 49.6 ms**, or **1.27× faster within XLang3**;
the candidate is still **18.2× slower than CPython 3.14.7**. The unpickle
result and cache ownership details are in the
[cross-activation cache report](vm-inline-cache-cross-activation-20260930.md).

## Follow-up: DeltaBlue-specific native VM timing (2026-09-30)

I rebuilt a separate instrumented Release executable and ran the unchanged
pyperformance 1.14.0 `bm_deltablue/run_benchmark.py` body through the direct
diagnostic runner. One process ran one benchmark iteration to capture startup
and import work; a second ran 21 iterations. The table subtracts the first
process from the second and divides by the 20 additional iterations. This
locates native costs in DeltaBlue itself; clock scopes perturb execution, so
these values are not benchmark scores or claimed speedups.

| Exclusive VM scope | Positive self-time share | Calls per iteration | Self ms per iteration | ns per call |
|---|---:|---:|---:|---:|
| VM loop control | 21.02% | 411,709 | 15.538 | 37.7 |
| VM frame switch | 15.27% | 98,090 | 11.282 | 115.0 |
| `CallLocalMethod` | 10.75% | 41,456 | 7.947 | 191.7 |
| `LoadModuleAttr` | 9.58% | 31,383 | 7.080 | 225.6 |
| `CallMethod` | 5.69% | 13,526 | 4.209 | 311.2 |
| `LoadLocalAttr` | 5.30% | 78,198 | 3.918 | 50.1 |
| `ReturnConst` | 4.97% | 19,496 | 3.675 | 188.5 |
| `Return` | 4.84% | 28,229 | 3.576 | 126.7 |
| `Compare` | 4.12% | 4,770 | 3.048 | 639.0 |

The horizontal bars show exclusive diagnostic self-time per DeltaBlue
iteration; each block represents about 1 ms and longer bars run left to right.

```text
VM loop control    15.538 ms |████████████████
VM frame switch    11.282 ms |███████████
CallLocalMethod     7.947 ms |████████
LoadModuleAttr      7.080 ms |███████
CallMethod          4.209 ms |████
LoadLocalAttr       3.918 ms |████
ReturnConst         3.675 ms |████
Return              3.576 ms |████
Compare             3.048 ms |███
```

Loop control, frame transitions, and the two method-call opcodes account for
about 53% of positive measured self-time; `LoadModuleAttr` adds another 9.6%.
The profile recorded roughly 98,000 frame-loop entries per workload
iteration. This is direct evidence that the gap is spread across XLang3's
shared dispatch and call/frame path, rather than being explained by local-name
binding alone. It does not mean that each frame-loop entry corresponds to a
separate source-level call: the VM re-enters its frame loop on both push and
return transitions.

`LoadModuleAttr` currently expands into a module-slot load followed by a
general attribute load in [`xlang_vm_ops_fused.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_fused.h#L26).
The common method path checks instance overrides and hooks before reaching its
class-version call cache in [`xlang_vm_ops_call.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_call.h#L718).
Those guards preserve Python behavior, but they leave more per-op work than
CPython's warmed specializations. CPython 3.14.7 guards direct instance-value
loads by type/layout version, has a method-load specialization, and prepares
exact-argument calls in its evaluator ([`LOAD_ATTR_INSTANCE_VALUE`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2148-L2170),
[`LOAD_ATTR_CLASS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2271-L2288),
[`LOAD_ATTR_METHOD_WITH_VALUES`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3321-L3336),
[`CALL_PY_EXACT_ARGS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3718-L3727)). XLang3's
indexed IR removes name decoding; it does not itself remove these dynamic
checks, helper dispatch, or frame transitions.

The actual warmed `BinaryConstraint.input` and `output` disassembly makes the
difference concrete. CPython executes `LOAD_FAST_BORROW`,
`LOAD_ATTR_INSTANCE_VALUE` for `self.direction`, `LOAD_GLOBAL_MODULE` for
`Direction`, `LOAD_ATTR_CLASS` for `FORWARD`, and `COMPARE_OP_INT`; it then
loads `v1` or `v2` with `LOAD_ATTR_INSTANCE_VALUE`. In
`BinaryConstraint.recalculate`, the calls to `input` and `output` use
`LOAD_ATTR_METHOD_WITH_VALUES` followed by `CALL_PY_EXACT_ARGS`. The saved
adaptive disassembly came from five warm iterations of the same official
benchmark source on CPython 3.14.7: [warmed DeltaBlue disassembly](data/cpython314-deltablue-warmed-dis-20260930.txt).
The corresponding XLang3 selector IR is in
[`deltablue-ir/run_benchmark.ir.txt`](data/deltablue-ir/run_benchmark.ir.txt).
Thus CPython retains dynamic semantics behind compact version guards at each
hot bytecode site, while XLang3 still dispatches the general class-attribute
path for `Direction.FORWARD`.

This profile sharpens the next target to generic VM dispatch and frame
handoff. It does not justify repeating the already neutral instance-layout
guard or inherited selector trials, and it does not attribute unpickle's
timing percentages to DeltaBlue. The latest ordinary-build rigorous comparison
is still **47.1 ± 4.5 ms** for XLang3 versus **2.73 ± 0.21 ms** for CPython
3.14.7, about **17.25× slower**; the full performance goal remains open.

Raw evidence and reproduction details:

- [One-iteration diagnostic log](data/deltablue-native-vm-timing-1loop-20260930.txt)
- [21-iteration diagnostic log](data/deltablue-native-vm-timing-21loops-20260930.txt)
- [CPython 3.14.7 warmed DeltaBlue disassembly](data/cpython314-deltablue-warmed-dis-20260930.txt)
- [Disassembly driver](../../benchmarks/diagnostics/dump_cpython_deltablue_dis.py); run it under CPython 3.14 with the pyperformance `bm_deltablue/run_benchmark.py` path to regenerate the warmed output.
- [Startup-subtracted CSV](data/deltablue-native-vm-timing-delta20-20260930.csv)
- [Startup-subtracted JSON](data/deltablue-native-vm-timing-delta20-20260930.json)
- [Temporary instrumentation patch](data/deltablue-native-vm-timing-probe-20260930.patch)
- Diagnostic `xlang3.exe` SHA-256: `4C7A9D288F0751BA43F4786F9E012E0944BBE9602869D12C0CE857CA65D508C3`.
- Diagnostic `xlang3_runtime.dll` SHA-256: `2EAC15B2A11A95C07B759D2AD1364F543FBD659ABA2F416209906CE21A17CCB0`.
- Source commit: `be989fce1a8d25aa5bc151e61b3a58f279c09cb5`; the ordinary source was restored after building the separate probe.
