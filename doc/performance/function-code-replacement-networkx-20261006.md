# Function code replacement and NetworkX (2026-10-06)

NetworkX's lazy Python decorators replace `func.__code__` after compiling a
specialized wrapper. The previous XLang3 setter stored that value as an
attribute but kept executing the old module/function ID. Fixing live keyword
defaults alone therefore did not unblock the three official NetworkX cases.

The [small reproduction](../../benchmarks/diagnostics/function_code_swap_probe.py)
returned the original result after replacement under XLang3, while CPython
3.14.7 returned the new result. This is a generic function-runtime defect.
NetworkX continues running its own Python implementation.

## Runtime design and performance guards

The candidate updates the function's executable module and function ID, while
retaining its globals, closure cells, defaults, name, qualified name, and
docstring. It validates the replacement's type and number of free variables
before changing execution. Existing active frames and suspended generators
keep their shared ownership of the original IR.

Positional defaults are reindexed against the new signature on mutation;
ordinary calls retain indexed binding. Exposed keyword defaults stay in the
shared live dictionary. Code-object identity is lazily materialized, and an
assigned object is retained exactly, including `code.replace()` metadata.

Call, method, constructor, and property specializations check the function's
code generation before reusing a body-derived plan. A code write advances
that generation, allowing reanalysis and specialization of the new body.
These checks belong to cached call/access paths, not every VM instruction.
Comments beside the function metadata and property cache explain why class
versions alone cannot guard this mutation.

Graph format version 4 preserves code identity, metadata overrides, and live
keyword-default aliases. The reader still accepts formats 1, 2, and 3. The
restoration test verifies an assigned code object shared with the graph root,
its filename override, preserved positional defaults, and successful execution
without the producer's Python source.

## Correctness validation

The registered [fixture](../../tests/fixtures/core/function_code_replacement.py)
passes on CPython 3.14.7 and XLang3. It covers:

- Code identity and calls at live cached sites, including expanded calls.
- Positional and keyword defaults, signature changes, and dictionary aliases.
- Bound methods, constructors, and property getters, setters, and deleters.
- Original globals and closure cells after replacement.
- Invalid code types and incompatible closures, through assignment and setattr.
- Already-running frames and suspended generators.
- `types.coroutine` flag replacement and `types.FunctionType` code identity.

The first complete fixture run caught a lost coroutine flag: `types.coroutine`
uses a replacement code object with `CO_ITERABLE_COROUTINE`. Preserving the
exact object corrected it. The subsequent complete fixture run, C++ runtime
and interpreter tests, SDK stream/keyword-call test, and graph
producer/consumer test all pass. The initial failure is retained in
[this log](data/function-code-replacement-fixtures-20261006.log); the passing
full fixture run is [here](data/function-code-replacement-fixtures-r2-20261006.log).

The build used the existing VS 18 environment and Ninja Multi-Config files.
The executable path remains
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
CPython is `C:\Python\Python314\python.exe`, version **3.14.7**.

Candidate SHA-256:

- Executable: `9AD9C13FF8BFC34068866FDEF2C1BF3875177B342F995A87BE41BC52F6FC6A61`.
- Runtime DLL: `DB4791ABFE1573055576F2E8FE97515EB4BD7719D621A226EC3A9C23A27E5B3B`.

## Performance gate and official workload outcome

The [complete default fixed Release gate](data/function-code-replacement-fixed-release-gate-20261006.json)
passed all 11 cases with the original 21 repeats, 5 warmups, and 10% threshold.
The largest candidate/baseline ratio was 1.036 (scalar arithmetic). Function
calls measured 0.993, constructors 1.003, property access 0.956, and JSON dumps
1.004. These ratios compare XLang3 builds; they are not speedups over CPython.
The accepted baseline executable and runtime DLL remain unchanged.

[Both focused metadata probes](data/function-metadata-candidate-probes-20261006.txt)
now give identical successful output under CPython 3.14.7 and this candidate.

All three official NetworkX definitions were attempted again in fast mode,
with the same dependency site and a 120-second full-case cap. They still fail
before timing and produce no benchmark JSON. The trace now executes the
compiled outer `argmap_read_adjlist_1` wrapper, proving that code replacement
is active. It then fails in the inner `parse_adjlist` lazy wrapper at
`func.__argmap__`. The [complete attempt log](data/pyperformance-xlang3-function-code-networkx-fast-20261006.log)
records that remaining defect. No NetworkX timing, speedup, or increased
full-suite completion count is claimed.

This is a correctness and runtime-design checkpoint, not achievement of the
goal to beat CPython. The most recent completed all-97 suite still has 53
completed cases, 44 failures, and a geometric CPython/XLang3 ratio of 0.15654
on 56 matched subtests (about 6.39 times slower for XLang3). Its full data and
chart remain in the [all-97 report](pyperformance-xlang3-bytearray-iadd-full-fast-20261006.md).

The gate and official attempt measured the local worktree candidate; existing
changes outside this function-metadata checkpoint remain in the worktree.
No unrelated changes are included in the checkpoint commit.
