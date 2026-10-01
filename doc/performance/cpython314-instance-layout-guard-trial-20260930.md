# CPython-style instance-layout guard trial (2026-09-30)

## Result

I compared CPython 3.14.7's warmed attribute and method paths with XLang3's
`LoadLocalAttr` and `CallLocalMethod` paths, then tested guarded instance-shape
caches in XLang3. Neither candidate produced a significant DeltaBlue gain, so
the experimental runtime changes were removed.

| Build | DeltaBlue mean ± standard deviation | Relative to CPython 3.14.7 |
| --- | ---: | ---: |
| CPython 3.14.7 reference | 2.73 ± 0.21 ms | 1.00× |
| XLang3 control | 47.1 ± 4.5 ms | 17.25× slower |
| XLang3 attribute-layout guard | 46.7 ± 4.0 ms | 17.10× slower |
| XLang3 method-shadow layout guard | 46.6 ± 5.1 ms | 17.07× slower |

Both XLang3 candidate comparisons were hidden by `pyperf compare_to` as not
significant. The second candidate also reported an 11% standard deviation.
These are rigorous-mode runs, but the host was noisy; the sub-1% differences
are not speedup claims. CPython's reference is the same official DeltaBlue
workload measured in the earlier rigorous comparison, not a simultaneous
sample. The CPython ratio is useful for scale, not a controlled result for the
candidate.

```text
CPython 3.14.7                 2.73 ms |█
XLang3 control                47.1  ms |███████████████████
XLang3 layout-guard candidate  46.6  ms |███████████████████
```

## CPython implementation and XLang3 gap

CPython 3.14.7's specialized `LOAD_ATTR_INSTANCE_VALUE` checks a type-version
tag and that managed instance values are valid, then loads directly from a
cached object offset. `LOAD_ATTR_METHOD_WITH_VALUES` checks the type version,
the instance-values state, and the shared dictionary-key version before
placing the cached method descriptor and receiver on the value stack. An
exact Python call then checks the function version and argument shape, sets
up the interpreter frame, and pushes it inside the evaluator. The relevant
CPython 3.14.7 definitions are [`LOAD_ATTR_INSTANCE_VALUE`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2148-L2170),
[`LOAD_ATTR_METHOD_WITH_VALUES`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3321-L3336),
and [`CALL_PY_EXACT_ARGS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3718-L3727).

XLang3 also caches an attribute-vector index at an IR site, but the existing
cache checks the stored name on each hit. Its `CallMethod` path separately
scans the receiver's compact attribute vector to preserve instance-level
method overrides before using the class method cache. The candidates interned
the exact ordered attribute-name shape per class and used layout identity to
skip those repeated string comparisons. Attribute loads improved by less
than the measurement noise; caching the method-shadow result did not improve
DeltaBlue either. A similar method-shadow guard had already been rejected on
the Richards workload, where it measured slower than the control. The
per-instance scan is therefore not a high-value standalone target.

## What the workload profile says

The saved DeltaBlue diagnostic counter run counted 82,727 `LoadLocalAttr`,
41,535 `CallLocalMethod`, 31,525 `LoadModuleAttr`, and 29,633
`CompareJumpIfFalse` instructions. These counts are diagnostic, not timing
attribution. The matching Python call profile records 14,054 `output`, 12,903
`execute`, and 11,707 `input` calls. The counter and call-profile files are
[`deltablue-vm-counters-20260929.txt`](data/deltablue-vm-counters-20260929.txt)
and [`deltablue-call-profile-20260929.txt`](data/deltablue-call-profile-20260929.txt).

The next target should reduce broader VM dispatch and Python-frame work. That
direction is consistent with the separate native-timer profile for
`unpickle_pure_python`, where `Call` and `CallMethod` account for about 23% of
positive instrumented self-time and VM frame switches about 11%. This is an
inference across two workloads, not a DeltaBlue time breakdown. The existing
[CPython VM comparison](cpython314-vm-comparison-20260930.md) and
[cross-activation cache trial](vm-inline-cache-cross-activation-20260930.md)
record the accepted cache-lifetime optimization and its remaining gap.

The August comparison also does not show a broad interpreter-throughput
regression: current main's measured `local_slots` loop slope was 22.7 ns per
iteration versus 28.4 ns in August, and its `function_calls` slope was 2.23 ns
versus 80.2 ns. Those microcases do not explain the complex benchmark gaps;
the August executable also cannot run the current Richards source. Details and
raw samples are in the [August-to-current analysis](pyperformance-xlang3-vs-cpython314-20260928.md#august-to-current-check-on-interpreter-microcases-2026-09-29).

## Reproduction data

- [XLang3 control JSON](data/attr-layout-shape-parent-deltablue-rigorous-20260930.json) and [log](data/attr-layout-shape-parent-deltablue-rigorous-20260930.log)
- [Attribute-layout candidate JSON](data/attr-layout-shape-candidate-deltablue-rigorous-20260930.json) and [log](data/attr-layout-shape-candidate-deltablue-rigorous-20260930.log)
- [Method-shadow candidate JSON](data/attr-method-layout-candidate-deltablue-rigorous-20260930.json) and [log](data/attr-method-layout-candidate-deltablue-rigorous-20260930.log)
- CPython 3.14.7 comparison: [merged rigorous samples](data/classmethod-attr-int-compare-cpython314-merged-rigorous-20260930.json)

The parent executable/runtime SHA-256 values are
`D56726FF584DE2717EFD006BD92C0AF8FA4B5CC73373565CB70C3F25B2059CCD` and
`72EBEB7D5E8847782AEDB6EC544BA7E3A643EAEC2E8B06BBCF891DCE1349F766`. The
attribute-only candidate values are
`CA6314D85DB37A161129C2A7EC696C8EAC08BDED574F63F42942263632157AA2` and
`5330AFA57277253305D599DB60F266CE7E653701A946C2E9A5C7BD3847518C48`. The
method-shadow candidate kept the same executable and used runtime DLL
`B8810C8ACCAE2886F323683B0F82EB0F6225B935D8006F541ABD79CF12BD6A74`.

Both candidates passed the rebuilt interpreter, runtime-value, and IR-codec
C++ tests and the full Python fixture runner. A fixture exercised repeated
attribute reads across insertion orders, attribute delete/re-add, class
methods across distinct instance shapes, instance method overrides, and
deletion of an override. It was removed with the rejected implementation.
