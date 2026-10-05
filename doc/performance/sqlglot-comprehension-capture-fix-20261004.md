# Restoring nested-generator captures used by `sqlglot` — 2026-10-04

The full pyperformance run recorded all four `sqlglot_v2` benchmarks as worker failures. Two independent XLang3 issues blocked them during package import: tuple-unpacked comprehension locals captured by nested generator expressions could remain uninitialized, and the regex matcher rejected the anchor-only lookbehind `(?<!^)` used by sqlglot 4.6.0.

The compiler fused adjacent comprehension target stores into `StoreLocalPair` before it knew which hidden target locals would become closure cells. The later cell-conversion pass recognized only `StoreLocal`, so the fused instruction wrote the plain local while the nested generator read an empty cell. Comprehension target stores now defer those two store fusions until capture conversion finishes. A fixture reproduces the exact nested-generator and tuple-target shape.

The native `_sre` matcher already translates eligible lookbehinds into deferred assertions. Its preflight check had rejected the simple negative lookbehind `(?<!^)` before that translation ran. The matcher now permits this anchor-only form through to its existing deferred-assertion path. The regex fixture compares the result against CPython 3.14.7.

## Results

The four affected official pyperformance definitions now complete in fast mode:

| Benchmark | XLang3 | CPython 3.14.7 | XLang3 slowdown |
|---|---:|---:|---:|
| `sqlglot_v2_normalize` | 1.64 s | 98.4 ms | 16.63× |
| `sqlglot_v2_optimize` | 744 ms | 41.6 ms | 17.88× |
| `sqlglot_v2_parse` | 20.6 ms | 1.01 ms | 20.42× |
| `sqlglot_v2_transpile` | 24.5 ms | 1.30 ms | 18.80× |

These results restore measurements for four definitions previously recorded as failures; they are not a speedup claim. The four-case pyperf geometric mean is still **18.38× slower** than CPython. The fixes leave sqlglot and its Python implementation untouched.

## Validation and artifacts

- Focused comprehension and regex fixtures pass.
- Complete `tests/run_fixtures.py` suite passed under Python 3.14.
- `xlang3_interpreter_tests.exe` and `xlang3_runtime_value_tests.exe` passed.
- sqlglot 4.6.0 imports successfully on the candidate with the Python 3.14 standard library.
- The four official pyperformance results are in [`sqlglot-closure-regex-fix-fast-20261004.json`](data/sqlglot-closure-regex-fix-fast-20261004.json); `pyperf compare_to` used the saved full CPython 3.14.7 reference.
- Candidate executable SHA-256: `2AE30FA9A56B11E9BF3298D5A48CA33357C9FF93C097FD29A8B9D485769A1E63`.
- Candidate runtime DLL SHA-256: `4FA34BEF554337E45F212CD018DDC54F409934F7E2DAD6382FC32AFA6943E810`.

The latest full 97-case comparison is recorded in [`pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.md`](../pyperformance-xlang3-context-field-index-cache-vs-cpython314-fast-20261004.md). It completes 52 definitions and records 45 failures or timeouts; all four SQLGlot measurements above are included. Across all 56 matched subtests, XLang3 is faster in four and has a CPython/XLang3 geomean of 0.15710×. The aggregate performance goal remains unmet.
