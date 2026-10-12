# Numeric dictionary construction before indexed DictSet

The optimized arithmetic and call microbenchmarks do not establish that all pure-Python programs run faster on XLang3. The [August/current report](august-vs-current-python314-microbench-20261004.md) reports gains between two XLang3 versions, including 6.33x for arithmetic and 3.67x for calls. Its CPython 3.14.7 driver does not make those ratios XLang3-versus-CPython speedups. The [full-suite comparison](pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008.md) measures broader workloads separately.

One concrete algorithmic difference is the VM's integer-key DictSet path. It scans the existing ordered entries for every insertion. The unchanged official BPE decoder comprehension constructs 1,024 distinct integer keys, requiring 523,776 existing-key checks. This establishes a quadratic construction path inside the official workload; its share of the complete BPE running time remains unmeasured. The [source diagnosis](data/l9-runtime-prerequisites-inspection-20261008/dict-set-int64-official-source-diagnosis-20261008.md) records the original benchmark, retained IR and source hashes. No pickle improvement follows from this finding, because its memo writes use ordinary SetItem.

CPython 3.14.7's [dictionary implementation](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c) uses a hash index over ordered entries. Its generic comparison retains the stored key, calls rich equality, propagates errors and restarts lookup if equality changed the table or key. Replacement publishes the new value before releasing the old owner. These details matter when adapting XLang3's own native dictionary implementation; this work does not replace any pure-Python library with C++.

The original four-group dictionary prerequisite check passed on CPython 3.14.7 and stopped on preserved S8 at the first group: dict.__init__ incorrectly cleared existing entries. Its later numeric groups were unexecuted. The [original failure receipt](data/dict-lazy-index-prerequisites-cpython3147-s8-reference-20261008.json), source and expected transcript remain unchanged.

Two fresh fixtures isolated exact source/output slices of those later groups. The root executed both CPython children first, followed by both preserved S8 children, once each. These were untimed correctness checks with strict output and empty-stderr requirements, a 120-second child limit and no retries. The [actual isolated receipt](data/dict-numeric-isolated-cpython3147-s8-reference-20261008.json) records all four children and unchanged source, Release and control hashes.

| Exact isolated group | CPython 3.14.7 | Preserved S8 |
| --- | --- | --- |
| Bool/Double-first dictionary literals | Passed | Passed; first PASS marker retained |
| Bool/Double-first comprehensions | Passed | Failed at line 19 on the first Bool-first map; Double-first map was not reached |
| Integer subclass whose equality returns False | Passed | Failed at line 13: the literal incorrectly merged its key with exact integer 1 |
| Integer subclass whose equality raises | Passed | Not reached after the preceding S8 failure |

The comprehension failure reproduces the VM shortcut's missing numeric-equivalence behavior. The subclass failure confirms a separate prerequisite for reusing the current integer setter: its broad integer classification and exact-query delegation can bypass the stored subclass's equality. Simply redirecting DictSet to that setter would preserve a correctness bug. A future fast route must prove the complete stored key set is callback-free and retain a correct runtime fallback for custom equality.

The [inspection directory](data/dict-numeric-isolation-inspection-20261008/) preserves the exact fixtures, expected transcripts, isolation proof, executed capture manager and manager proof. These copies retain their original historical path pins; they are inspection material rather than a standalone portable runner. Raw receipt and stream bytes are preserved. The receipt SHA-256 is `4b3cf998941f18789617849eb2a289bb83844639b651227b8115d05b13a36294`.

No engine change, new timing result, performance gate or full-suite refresh is included. The exact validated S8 checkpoint remains the current runtime. This evidence narrows the next dictionary change; it neither establishes a whole-suite root cause nor claims a new speed gain.
