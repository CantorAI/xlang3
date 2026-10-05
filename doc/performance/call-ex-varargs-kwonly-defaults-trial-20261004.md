# `CALL_FUNCTION_EX` binding for `*args` plus keyword-only defaults — 2026-10-04

## Result

Retained as a small, workload-confirmed optimization. On the official
pyperformance 1.14.0 `async_tree_eager --fast` case, two source-matched runs
per build combine to **2.83 s ± 0.03 s** for the control and **2.80 s ± 0.05
s** for the candidate. `pyperf compare_to` reports the candidate **1.01×
faster**, statistically significant at `t=3.57`. This closes only about 1% of
the roughly 32× gap to CPython 3.14; it is not a material resolution of the
overall performance objective.

## Why this shape

The eager async-tree workload repeatedly calls `asyncio.gather(*children)`.
Its Python signature is `(*coros_or_futures, return_exceptions=False)`. The
generic binder expanded the exact list into an intermediate positional vector,
then copied its values into another varargs vector before creating the bound
tuple. The guarded path now creates that final tuple directly and copies the
live keyword-only default into the bound frame.

The guard requires one exact list or tuple star, no explicit positional
arguments, no keyword or `**` expansion, a fixed `*args`-first signature, and
valid fixed keyword-only defaults. Subclasses, custom iterators, multiple
stars, mutable/dynamic defaults, and other call forms remain on generic
binding so iteration hooks and binding semantics remain observable. The
comment explaining the common call shape and guard is beside the fast path in
`src/executor/xlang_vm/xlang_vm_loop.cpp`. The fixture
`tests/fixtures/core/call_ex_varargs_keyword_defaults.py` checks default
values, live default mutation, empty-tuple identity, and list-subclass
iteration against CPython 3.14.

## Evidence and validation

Four balanced official pyperformance fast runs used CPython **3.14.7**,
pyperformance **1.14.0**, and the fixed Release executable path. The source
matched both candidate repeats:

| Order | Build | Mean ± standard deviation |
|---|---|---:|
| 1 | Control | 2.84 s ± 0.03 s |
| 2 | Candidate | 2.79 s ± 0.04 s |
| 3 | Candidate | 2.81 s ± 0.05 s |
| 4 | Control | 2.83 s ± 0.03 s |

The fixed 11-case Release regression gate passed all cases with seven
order-balanced pairs and two warmups; the worst candidate/baseline ratio was
`gc_traversal` at **1.012×**, below the 1.10 limit. The interpreter C++ tests
passed, and the new fixture matched CPython 3.14.7. Diagnostic perf counters
recorded four direct fast bindings in the fixture.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `00133F5E28BA8E6565F8447A3C758D7096F8906EF3401847407E175D44FF0E0A` | `381198090024EE4236BA87764C62D19DC660149CD619AC5595F3CC24FC37856F` |
| Candidate | `78759AC13404C2A05ED26F2ADA691E094BF7944C5C58952FC4635DD4116171AC` | `AA32D00FEBF2AC72666026D58DF544F6D649F9C2C14009227A90098D51B5EE61` |

Raw control and candidate runs, worker logs, and the merged pyperf files are
preserved under `data/call-ex-varargs-kwonly-*20261004.*`. The completed
97-definition run and CPython 3.14 comparison are recorded in
[`pyperformance-xlang3-call-ex-varargs-keyword-defaults-vs-cpython314-20261004.md`](pyperformance-xlang3-call-ex-varargs-keyword-defaults-vs-cpython314-20261004.md).
The full suite completed 46 definitions, failed or timed out 51, and matched
50 subtests; the geometric mean ratio was **0.16205× CPython/XLang3** (about
**6.17× slower**). The broad objective remains open, and subsequent work must
target a substantially larger, repeatable gain.
