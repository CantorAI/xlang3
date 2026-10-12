# Independent R3 correction review

Frozen incremental patch SHA-256 `1cd96169bcd6bdd46d7d5ac8b2d34abfb11d2e74ea37ab19d59018df99e8ab24` verified. Static read only; no runtime, compiler, build, or actual source/test mutation by this reviewer.

The inherited-only `index != slot->index` rejection closes the dispatch discrepancy recorded in the R2 independent review. It retains the accepted own-slot branch, class/version negative-cache sentinel, missing-storage Retry and native hook guards. Displaced layouts continue through their original descriptor dispatch.

The added C++ test establishes raw index 0 versus effective index 1, checks repeated Descriptor-only rejection, then executes actual VM local and module reads requiring nine `__getattr__` calls while raw storage is absent. After initializing raw index 0, it verifies the existing name-remapped value and no additional fallback. This tests observable dispatch rather than only duplicating the new comparison.

Verdict: ready for controlled correctness/build/paired trial. No remaining static blocker in the reviewed scope. Performance and runtime acceptance still require the unchanged controls and gates.
