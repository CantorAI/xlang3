# Guarded direct class-attribute loads (2026-09-30)

This trial follows CPython 3.14.7's warmed `LOAD_ATTR_CLASS` path. It adds a
guarded cache for direct, non-descriptor values stored on a class, so repeated
reads such as `Direction.FORWARD` can copy the mapped `Value` without repeating
general attribute resolution. Python libraries and benchmark bodies remain
Python; this change specializes XLang3's VM and does not reimplement a
pure-Python library in C++.

## What CPython does

After warmup, CPython specializes class attribute access at the bytecode site.
The DeltaBlue `BinaryConstraint.input` and `output` methods load
`Direction.FORWARD` through `LOAD_ATTR_CLASS`; that handler checks type-version
guards and returns the value from the class dictionary. See CPython 3.14.7's
[`LOAD_ATTR_CLASS` handler](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2271-L2288)
and [specialization logic](https://github.com/python/cpython/blob/v3.14.7/Python/specialize.c#L1345-L1374).
The saved warmed disassembly of the same official benchmark is in the
[DeltaBlue implementation analysis](deltablue-cpython314-implementation-analysis-20260930.md).

XLang3's IR has indexed names, but indexing avoids decoding a name; it does not
avoid checking metaclass hooks, descriptors, and the dynamic class lookup path
on each execution. This cache gives one hot IR site a compact version guard,
similar to CPython's warmed bytecode cache.

## XLang3 change and correctness guards

`AttrSiteKind::ClassValue` stores a non-owning pointer to a direct class-map
value. A hit requires both the metaclass and receiver-class versions to match.
The pointer is checked only after those guards. Class-map rehash preserves
element references; class mutation changes its version, so replacing or
deleting the mapped value falls back through generic resolution before the
pointer can be used.

The fast path is deliberately narrow. It skips dunder names and classes with a
custom metaclass `__getattribute__`; it caches only a direct, non-descriptor
class value when the metaclass has no matching data descriptor. Properties,
descriptors, inherited lookups, hooks, and all other cases continue through
the normal attribute implementation. The code comment beside the fast path
records these guards and the pointer-lifetime rule.

The fixture warms a read, replaces the direct class value, adds a metaclass
property that takes precedence, then removes the property. Both XLang3 and
CPython 3.14.7 produce the expected values in
[`class_dynamic_attrs.py`](../../tests/fixtures/core/class_dynamic_attrs.py).
The complete 302-case Python fixture runner passed on the candidate.

## Results

Two rigorous, opposite-order pyperf pairs show a repeatable DeltaBlue gain.
Each runtime executes the same pyperformance 1.14.0 benchmark body.

| Workload | Parent | Candidate | Candidate speedup | CPython 3.14.7 | Candidate vs CPython |
|---|---:|---:|---:|---:|---:|
| `deltablue`, pair 1 | 45.9 ± 2.4 ms | 41.0 ± 4.6 ms | 1.12× | 2.73 ± 0.21 ms | 0.067× (15.0× slower) |
| `deltablue`, reverse pair | 46.8 ± 4.3 ms | 40.9 ± 4.5 ms | 1.15× | 2.73 ± 0.21 ms | 0.067× (15.0× slower) |
| `unpickle_pure_python` | 3.32 ± 0.27 ms | 3.20 ± 0.24 ms | 1.04× | 0.165 ms | 0.052× (19.4× slower) |

The rigorous unpickle comparison is significant in `pyperf compare_to -v`
(`t=3.63`), but small. The cache targets direct class-value loads, so
DeltaBlue is the more relevant measure. Its two pairs agree despite pyperf's
host-variability warnings. Averaging the two pair means gives 46.35 ms for the
parent and 40.95 ms for the candidate, a 1.13× within-XLang3 speedup. The
candidate remains far slower than CPython; this optimization advances the
goal but does not meet it.

```text
Elapsed time (shorter is faster; each block is about 1 ms)

CPython 3.14.7       2.73 ms |███
XLang3 parent        46.35 ms|██████████████████████████████████████████████
XLang3 candidate     40.95 ms|█████████████████████████████████████████
```

The candidate passed the 11-case order-balanced Release regression gate
against its immediate parent. The highest observed case ratio was 1.034× for
`function_calls`, within the gate's 10% limit. The complete Python fixture
runner also passed. The candidate's full 97-definition `--fast` run has now
completed: 31 definitions produced timings, 66 failed, and 35 subtests matched
CPython. The geomean was 0.14137× CPython time/XLang3 time (about 7.07× slower).
The [full-suite report](pyperformance-xlang3-vs-cpython314-20260930.md)
contains the complete candidate status list, comparison CSV, chart, and raw
log; it also retains the earlier full-suite snapshot as the before-change
comparison.

## Reproduction data

All timings use Release executables on the same Windows 11 x86-64 host. The
candidate `xlang3.exe` and `xlang3_runtime.dll` SHA-256 values are
`4C7A9D288F0751BA43F4786F9E012E0944BBE9602869D12C0CE857CA65D508C3` and
`FBD5C3822BB83CB04A9302D165221670C4AF7ACB0CF4A6816267CC9F63D21707`.
The immediate-parent hashes are
`D3FCD013158F522B30ABD8781DDABE67C3C2755AC19BCA4FE5D923D67E788D95` and
`22A716FDE9C9A39E1D0A6AF87D6FDCF49CA02B6EECF276B7E627BBA94EBB034F`.

- DeltaBlue pair 1: [parent JSON](data/deltablue-class-value-parent-rigorous-20260930.json), [candidate JSON](data/deltablue-class-value-candidate-rigorous-20260930.json); [parent log](data/deltablue-class-value-parent-rigorous-20260930.log) and [candidate log](data/deltablue-class-value-candidate-rigorous-20260930.log).
- DeltaBlue reverse pair: [parent JSON](data/deltablue-class-value-parent-repeat-rigorous-20260930.json), [candidate JSON](data/deltablue-class-value-candidate-repeat-rigorous-20260930.json); [parent log](data/deltablue-class-value-parent-repeat-rigorous-20260930.log) and [candidate log](data/deltablue-class-value-candidate-repeat-rigorous-20260930.log).
- Unpickle: [parent JSON](data/unpickle-class-value-parent-rigorous-20260930.json), [candidate JSON](data/unpickle-class-value-candidate-rigorous-20260930.json), [parent log](data/unpickle-class-value-parent-rigorous-20260930.log), and [candidate log](data/unpickle-class-value-candidate-rigorous-20260930.log).
- Immediate-parent Release gate: [JSON report](data/class-value-immediate-parent-gate-20260930.json) and [console log](data/class-value-immediate-parent-gate-20260930.log).
