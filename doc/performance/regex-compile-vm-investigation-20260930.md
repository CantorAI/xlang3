# Regex compile: Python-level VM cost

`regex_compile` remains much slower in XLang3 than CPython 3.14. Profiling
places repeated work in Python's regex parser/compiler, so optimizations must
improve generic XLang3 compiler, IR, VM, or runtime paths. Keep `re._parser` and
`re._compiler` implemented in Python. A native XLang3 module is appropriate
only where CPython itself supplies a native module, such as `_sre`, while
preserving its Python-facing import name and compatible module API/ABI.

## Current paired result

The official pyperformance 1.14 `regex_compile` case was run with
`--rigorous` on the same Windows x64 machine, using the Release XLang3 build at
`29b4d26` as control and CPython 3.14 as the reference:

| Runtime | Mean | Standard deviation |
| --- | ---: | ---: |
| CPython 3.14 | 95.8 ms | 1.3 ms |
| XLang3 control | 1.41 s | 0.03 s |
| XLang3 candidate | 1.33 s | 0.03 s |

`pyperf compare_to` finds the candidate **1.06× faster** than control. Against
CPython, the candidate is still about **13.9× slower**, or runs at about
**0.072×** CPython's speed. The optimization therefore makes a measurable but
limited improvement; it does not close the larger execution-speed gap.

Raw results:

- [CPython 3.14 rigorous](data/pyperformance-regex-compile-cpython314-rigorous-20260930.json)
- [XLang3 control rigorous](data/pyperformance-regex-compile-getitem-dispatch-control-rigorous-xlang3-20260930.json)
- [XLang3 candidate rigorous](data/pyperformance-regex-compile-getitem-dispatch-candidate-rigorous-xlang3-20260930.json)
- [Control fast](data/pyperformance-regex-compile-getitem-dispatch-control-fast-xlang3-20260930.json) and [candidate fast](data/pyperformance-regex-compile-getitem-dispatch-candidate-fast-xlang3-20260930.json)

## VM change

The profile recorded about 470,000 Python call events in XLang3 and 521,000
in CPython. Frequent XLang3 calls included `re._parser.__getitem__` (119,078),
`Tokenizer.__next__` (65,306), and `_parser.get` (48,386). `SubPattern.__getitem__`
is Python code with a slice branch and a separate scalar-index branch.

The first probe attempted to inline only the simpler method shape
`return self.attribute[index]`. It did not match the actual parser method and
`pyperf compare_to` found no significant change (both control and candidate
were 1.41 s ± 0.03 s); that shortcut and its fixture were removed.
Its paired [fast](data/pyperformance-regex-compile-getitem-wrapper-control-fast-xlang3-20260930.json)
and [rigorous](data/pyperformance-regex-compile-getitem-wrapper-control-rigorous-xlang3-20260930.json)
control data, plus [fast](data/pyperformance-regex-compile-getitem-wrapper-candidate-fast-xlang3-20260930.json)
and [rigorous](data/pyperformance-regex-compile-getitem-wrapper-candidate-rigorous-xlang3-20260930.json)
candidate data, are retained to avoid repeating that probe.

The retained candidate optimizes only dispatch for a plain Python
`__getitem__` defined directly on the object's class. The VM caches that method
under the class version and passes `self` plus the index directly to the normal
Python frame path. The Python method still performs indexing, branching, and
raises its original exceptions. This avoids a temporary bound-method object
and generic callable dispatch without replacing any standard-library code.

Correctness coverage includes cache invalidation after class rebinding,
inherited methods, descriptors, instance-level shadowing, closures, and
`IndexError`. The fixture passes under XLang3 and CPython 3.14; the full fixture
suite passes under XLang3. The complete fixed 11-case Release regression gate
also passes; [its JSON report](data/release-regression-getitem-python-method-dispatch-20260930.json)
records the comparison to the preserved fixed baseline. A second
[11-case control-to-candidate report](data/release-regression-getitem-python-method-dispatch-vs-control-20260930.json)
shows the ten non-subparser cases within 1% of the unmodified `29b4d26` control;
`subparsers` improved by 6.4%. `deepcopy_memo` improved by 0.3%, while
`json_dumps` was 0.7% slower.

## Diagnostic evidence

The `sys.setprofile` wall times are instrumented and are not benchmark scores.
The unprofiled VM-counter run completed the direct workload in about 1.56 s
while recording 640,437 native calls and hundreds of thousands of container,
local, attribute, and control-flow operations. Full counts are in
[the VM-counter output](data/regex-compile-vm-counters-xlang3-20260930.txt).
This supports continued work on common Python execution paths while keeping
pure-Python library implementations in Python.
