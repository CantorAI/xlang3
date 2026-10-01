# Balanced MSVC PGO trial (2026-10-01)

This trial tests whether whole-program profile-guided code generation can reduce
XLang3's native VM overhead without specializing the build for only one
pyperformance workload. The source is the committed `646107e` Release tree.

## Fixed Release gate

The Release baseline and balanced PGO candidate were built with MSVC 19.51. The
candidate uses `/GL` and `/USEPROFILE`; its profile combines the fixed 11-case
gate with a smaller, partially timed-out pure-Python pyperformance training
batch. The two DLLs export the same 20,353 names. The order-balanced fixed gate
passed every case. Each row shows candidate time divided by baseline time; the
rightward bar shows the corresponding speedup (`1 / ratio`).

| Case | Candidate / baseline | Speedup | Relative speed |
|---|---:|---:|---|
| `local_slots` | 0.745x | 1.34x | ██████████████████ |
| `scalar_arithmetic` | 0.676x | 1.48x | ████████████████████ |
| `range_for` | 0.896x | 1.12x | ███████████████ |
| `function_calls` | 0.682x | 1.47x | ████████████████████ |
| `class_construct` | 0.824x | 1.21x | ████████████████ |
| `list_append` | 0.860x | 1.16x | ███████████████ |
| `property_access` | 0.751x | 1.33x | ██████████████████ |
| `deepcopy_memo` | 0.869x | 1.15x | ███████████████ |
| `json_dumps` | 0.887x | 1.13x | ███████████████ |
| `gc_traversal` | 0.985x | 1.02x | ██████████████ |
| `subparsers` | 0.912x | 1.10x | ███████████████ |

The geometric mean is **1.22x faster** across this gate. The weakest result,
`gc_traversal`, is effectively neutral at 1.02x; the other ten are faster.
These numbers compare two XLang3 builds and do not imply parity with CPython.

## Why the first profile failed

A profile trained only on a few Python-heavy cases improved those cases by
roughly 2–7%, but made general workloads much slower: the gate measured
`scalar_arithmetic` at 1.44x baseline time and `range_for` at 1.59x. MSVC's PGO
link compiled many unobserved functions for size, so the narrow workload
profile optimized the wrong mix.

The accepted trial profile adds one run of each fixed gate case before the
PGO link. That broad training changed the outcome: all 11 cases passed and the
gate's geometric mean improved by 22%. This is why PGO training must include
both interpreter-heavy application workloads and small dispatch-focused cases.

## CPython source comparison

The pure-Python pyperformance pickle benchmark runs CPython 3.14.7's
`Lib/pickle.py` `_Unpickler`, the same source XLang3 executes. XLang3's indexed
locals remove source-name lookups, but the hot loop still performs dynamic
dispatch-table indexing and generic calls. CPython's warmed evaluator
specializes those bytecode sites to `BINARY_OP_SUBSCR_DICT` and
`CALL_PY_EXACT_ARGS`, then pushes a compact interpreter frame directly into its
dispatch stack. XLang3 keeps calls inside its VM too, but its generic `GetItem`,
`Call`, frame setup, and frame-view publication still do more work per
operation. The source comparison and native cost attribution are documented in
[`cpython314-vm-comparison-20260930.md`](cpython314-vm-comparison-20260930.md).

That comparison also prevents a misleading fix: this benchmark intentionally
uses the pure-Python `_Unpickler`, so replacing its algorithm with C++ would
break the benchmark contract. Native accelerators remain appropriate for
CPython's native extension modules, such as `_pickle` or `_json`; the large
pure-Python gap belongs in XLang3's VM dispatch and call/frame path.

The complete 97-definition PGO run, including its 41 matched timed subtests,
60 failure statuses, CPython ratios, and left-to-right chart, is recorded in
[`pyperformance-xlang3-msvc-pgo-vs-cpython314-20261001.md`](pyperformance-xlang3-msvc-pgo-vs-cpython314-20261001.md).

## Validation limits

The fixed performance gate passed. CTest did not complete: 50 cases passed, a
Visual Studio debug-profile test failed because it expects the checkout's
default executable path, and the large CLI fixture runner stopped making
progress. The remaining test status was not observed. The full pyperformance
run and its per-case timeouts and worker failures are preserved in the linked
comparison report rather than being omitted.

The PGO build flags are opt-in and only apply to the MSVC Release runtime. A
future default Release build should use a profile trained from the complete
pyperformance corpus plus the fixed gate, then repeat the full correctness and
ABI checks.
