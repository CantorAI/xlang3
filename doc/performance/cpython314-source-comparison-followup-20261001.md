# CPython 3.14 source comparison: set membership and regex compilation

This follow-up compares the current XLang3 Release runtime with CPython 3.14.7
at two hot paths. The comparison confirms that these benchmarks run their
standard-library Python code in XLang3. The remaining regex gap is not a
CPython fallback or a missing native `_sre` module; XLang3 already registers
its own `_sre` implementation. The pure-Python `copy`, `re._parser`, and
`re._compiler` modules remain Python, as required.

## `deepcopy_memo`: replace linear set membership with a lazy index

CPython's pure-Python `copy.deepcopy` tests `cls in _atomic_types` as it walks
objects. CPython 3.14.7's [`Objects/setobject.c`](https://github.com/python/cpython/blob/v3.14.7/Objects/setobject.c)
uses a hash-table probe for that membership test. XLang3's `SetObject` kept
insertion-ordered `Value` entries and cached hashes, but membership scanned the
whole vector.

XLang3 now builds a lazy bucket index on the first lookup of a set with at
least eight entries. The ordered entries remain authoritative; mutations
advance a content version and rebuild the index on its next use. A second
identity chain preserves lookup of the exact object when a native helper
constructed a set without a `Runtime` available to call that object's Python
`__hash__` method.

The diagnostic absent-membership medians below use the same generated class
objects and 5 × 5,000 lookups per size. This isolates the scaling effect; it
does not replace the official pyperformance result.

| Set size | XLang3 before | XLang3 indexed | CPython 3.14.7 |
| ---: | ---: | ---: | ---: |
| 4 | 1,043 ns | 1,053 ns | 15.4 ns |
| 8 | 1,069 ns | 1,037 ns | 14.9 ns |
| 16 | 1,150 ns | 1,023 ns | 15.3 ns |
| 32 | 1,323 ns | 1,067 ns | 18.1 ns |
| 64 | 1,718 ns | 1,075 ns | 17.2 ns |
| 128 | 2,338 ns | 1,091 ns | 17.3 ns |

For the official `deepcopy_memo` benchmark, pyperf measured 588 μs before the
index and 572 μs after it, or **1.03× faster**. The other two deepcopy
subtests were not significant, and the three-case geometric mean rounded to
1.00×. The specific membership probe therefore improves with set size, while
the complete deepcopy workload sees a small gain. XLang3 remains much slower
than CPython on this benchmark.

Evidence: the [reproducible membership probe](../../benchmarks/diagnostics/set_membership_scaling.py),
[control data](data/set-membership-scaling-xlang3-control-20261001.txt),
[indexed XLang3 data](data/set-membership-scaling-xlang3-final-20261001.txt),
[CPython data](data/set-membership-scaling-cpython314-final-20261001.txt),
[official pyperf control](data/deepcopy-set-index-v2-control-fast-20261001.json),
and [official pyperf candidate](data/deepcopy-set-index-v2-candidate-fast-20261001.json).

## `regex_compile`: defer matcher preparation, then profile the Python path

The official benchmark gathers patterns from the Effbot and V8 workloads,
calls `re.purge()` for each one, and then calls `re.compile()`. It never
matches the returned patterns. CPython 3.14.7 runs the same Python
[`re._parser`](https://github.com/python/cpython/blob/v3.14.7/Lib/re/_parser.py)
and [`re._compiler`](https://github.com/python/cpython/blob/v3.14.7/Lib/re/_compiler.py)
code, then calls the native [`_sre` compiler](https://github.com/python/cpython/blob/v3.14.7/Modules/_sre/sre.c)
through the Python [`re` wrapper](https://github.com/python/cpython/blob/v3.14.7/Lib/re/__init__.py).

XLang3 also uses its own native `_sre` module. Before this change, `_sre.compile`
translated the parsed SRE program to the host `std::regex` representation
while constructing a pattern, even when the caller only compiled and stored
the pattern. That work is now deferred until the first match or search. The
pattern's SRE code, flags, group data, and public API remain intact; the native
matcher state is prepared once, on demand, behind a mutex.

The official rigorous result is **1.26 s ± 0.06 s** for XLang3 versus
**95.8 ms ± 1.3 ms** for CPython 3.14.7: XLang3 is **13.13× slower**. Against
the immediately preceding XLang3 candidate at 1.33 s, deferring unused matcher
setup measured **1.06× faster**. Pyperf warns that the current samples are not
stable enough for its strict variation target; these means estimate the size
of the improvement and remaining gap.

```text
CPython 3.14.7     95.8 ms |█
XLang3             1.26 s  |█████████████
```

Evidence: [current rigorous XLang3 result](data/regex-compile-final-xlang3-rigorous-20261001.json),
[prior XLang3 result](data/pyperformance-regex-compile-getitem-dispatch-candidate-rigorous-xlang3-20260930.json),
[CPython 3.14.7 result](data/pyperformance-regex-compile-cpython314-rigorous-20260930.json),
and [current Release regression gate](data/regex-set-final-regression-gate-20261001.json).

## What the CPython implementation comparison says about the remaining gap

After warming the pyperformance pattern set on CPython 3.14.7, adaptive
disassembly shows specializations such as `LOAD_ATTR_METHOD_WITH_VALUES`,
`CALL_PY_EXACT_ARGS`, `BINARY_OP_SUBSCR_LIST_INT`, `FOR_ITER_LIST`, and
`STORE_SUBSCR_LIST_INT` in the regex parser/compiler path. The definitions in
[`Python/bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c)
guard these common cases and enter exact-argument Python frames directly.
The warm disassembly and specialization counts are in
[the CPython opcode report](data/regex-compile-cpython314-adaptive-opcodes-20261001.txt).

The XLang3 source IR for the same `_parser.py` methods uses indexed locals,
fused local loads, guarded list operations, and direct `CallMethod` paths.
The VM also caches stable class methods by class version. Earlier measurements
found that extra method-shadow and instance-layout guards were neutral or
slower, so this follow-up does not repeat them. See the
[CPython VM investigation](cpython314-vm-comparison-20260930.md) and the
[regex `__getitem__` dispatch trial](regex-compile-vm-investigation-20260930.md).

A diagnostic pass over 200 patterns sampled evenly from the full corpus
counted about 800,000 CPython instruction events in regex/enum Python code
and 515,466 XLang3 IR dispatches for the corresponding workload pass. The
CPython monitoring hook changes the interpreter path, and the XLang3 delta
includes its direct benchmark body, so these totals are not an instruction
for-instruction comparison or a timing result. They do not establish how much
of the official gap comes from operation counts or the cost of each operation.
The call profiles find the same hot parser/compiler functions in both runtimes,
which makes those shared paths the next targets for native runtime profiling.

This narrows the next work to reducing the cost of generic IR dispatch,
per-operation runtime checks, and Python frame/call handling. It does not
justify moving `re._parser` or `re._compiler` to C++. New native XLang3 modules
remain appropriate only for modules that CPython implements natively, such as
`_sre`.

Diagnostic evidence: [CPython call profile](data/regex-compile-call-profile-cpython314-20261001.txt),
[XLang3 call profile](data/regex-compile-call-profile-xlang3-20261001.txt),
[XLang3 counters for one extra sampled pass](data/regex-compile-xlang3-vm-counter-delta-200-20261001.txt),
and [CPython instruction-event counts](data/regex-compile-cpython314-instruction-counts-20261001.txt).

## Validation

The full Release build succeeded, all **53/53 CTest tests passed**, and the
complete 11-case fixed-baseline gate passed. The gate JSON preserves every
case and the measured ratio; it is separate from the CPython speed comparison.
The refreshed all-97 pyperformance run finished: 37 definitions completed,
60 failed or timed out, and 41 subtest timings matched the saved CPython
reference. Two were faster and 39 slower, with a 0.14048x geometric ratio
across this measured subset. The
[full report and horizontal chart](pyperformance-xlang3-set-regex-candidate-full-fast-20261001.md)
retain every definition, timing, and failure. Twenty-six definitions reported
missing dependency imports; their package sources exist in the saved CPython
benchmark environment and need to be exposed for a fair rerun.

The expanded `set_hash_caching` fixture warms the large-set index before
removal, insertion, pop, update, intersection/difference/symmetric updates,
reinitialization, and clear. It also exercises collision chains and equality
that clears or replaces the probed entry, through both `in` and
`set.__contains__`. After the full run terminated, that expanded fixture
passed under both CPython 3.14.7 and the unchanged XLang3 Release candidate.
