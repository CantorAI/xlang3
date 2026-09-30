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

The evidence therefore points to adaptive, guarded fast execution across
ordinary instance loads, method dispatch, and small Python frames—not a C++
port of a pure-Python library. Existing specialized operations must preserve
the generic path for descriptors, dynamic instance attributes, subclass
overrides, hooks, tracing, and monitoring. The next broad optimization should
make common stable object shapes cheap at every hot read and call site, then
measure the complete fixed Release gate and official target workload.

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
