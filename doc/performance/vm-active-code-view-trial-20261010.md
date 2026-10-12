# Active instruction view trial: rejected

The VM trial captured the active function's instruction pointer and instruction
count once per frame switch instead of reading the vector on each dispatch.
Disassembly confirmed that instruction-count division moved out of the loop.
The original `json_dumps` benchmark did not demonstrate a speed improvement,
so the engine change and its fixture registration were reverted.

| Original workload, XLang3 Release | Mean time |
| --- | ---: |
| Accepted R7b control | 35.806889 ms |
| Instruction-view candidate | 35.996586 ms |
| Control time / candidate time | 0.994730× |

These are two sequential fast-mode runs with 20 scored values each. Their
standard deviations were 3.38 ms and 3.59 ms. This is an unpaired comparison;
the small difference does not establish either a gain or a regression.
It is not a comparison against CPython. Passing the regression gate is not
evidence that an optimization helps.

Validation covered the code-replacement fixture against CPython 3.14.7,
407 core fixtures, 11 compatibility fixtures, three expected failures,
all 55 CTest checks, two API checks, and the complete fixed regression gate
(21 repetitions, five warmups, 0.10 tolerance). The 55 CTest checks passed
semantically, but concurrent external test activity invalidated that phase
for timing. Its original invalid measurement status remains preserved;
the later continuation reused only its untimed correctness result.
The fresh gate and both original JSON runs passed their activity and identity
checks. No shortened benchmark workload was used.

The final validation is
[the R5 receipt](data/vm-active-code-view-r5-validation-20261009-validation.json).
[The rejection and restoration receipt](data/vm-active-code-view-rejected-20261010.json)
records the exact accepted source and Release hashes restored at the fixed
`build-repro/main-verify-20261006/Release` path. The committed UTF-8 harness
repair was retained. The fixed baseline and six unrelated dirty files were
unchanged. Candidate build objects remain on disk, so the next build must
recompile changed source; copied accepted binaries do not recertify those objects.

The retained patches and three-part fixture document the active-frame lifetime
assumption. A future attempt must show a changed mechanism and measurable
benefit rather than repeat this hoist. Pure-Python libraries were unchanged.
The full-suite goal of beating CPython 3.14.7 remains unfinished.
