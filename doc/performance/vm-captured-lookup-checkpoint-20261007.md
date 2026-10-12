# Ordinary calls to captured dictionary lookup functions

Ordinary CALL, CALL_LOCAL and CALL_GLOBAL now share the captured-item IR shortcut
with native callback entry, including warmed UserFunction and saved bound-method
sites. Indexed names were already cheap: the cost removed is the Python frame's
register/local setup and ownership work for a proven nonfallible dictionary hit.
Current code/signature and live closure cells are checked every call. The Python
body and Counter remain Python; no CPython native implementation is reused.

Only ordinary CALL sites opt into the shared trampoline. Keywords/expansion,
missing/default arguments, generators/async functions, misses, overrides,
descriptors, custom key protocols and active observability keep original entry.
The existing canonical intrinsic dictionary hit guard remains unchanged. The
shortcut neither advances the instruction nor pushes a frame. Its caller uses
Next, preserving monitoring refresh after an old output finalizer; property,
subscription and constructor contexts retain their original frame/return paths.
Comments record these performance and correctness constraints.

## Correctness and fixed gate

The candidate passed **375 core fixtures**, 11 compatibility sections, three
expected failures, eight C++/SDK/graph checks, and the unchanged fixed gate:
11 cases, 21 paired repeats, five warmups, 10% tolerance. Python 3.14.7, preserved
control and candidate all passed the ten-part dedicated fixture. Coverage includes
warm direct/bound calls, cell/code replacement, binding/errors, overrides and key
protocols, return contexts, trace/profile/local monitoring and finalizer refresh.
The existing Release build/run path is unchanged. Accepted control preserves
140 Release files; validation fingerprints the compiled inputs and binary pair.

The first finalizer fixture constructed its object in a module-level dictionary
literal. Both control and candidate failed its prompt-release assertion, while
CPython passed. Construction in a completed function isolates CALL-output release
from module temporaries and all three then passed. The original fixture, failed
logs and source hashes are retained. **Module temporary lifetime remains an
unresolved correctness difference**, not a repaired engine behavior.

An unrelated xMind build delayed the early check. It was left alone; observation
timeouts did not trigger build/benchmark restarts. Engine/build/benchmark phases
were serial. The quick paired check preceded the expensive validation to reject
bad or ineffective candidates sooner; read-only reviews ran in parallel.

## Performance evidence

Official BPE means: CPython 3.14.7 **3.586 s**, preceding XLang3 **26.090 s**, candidate **26.016 s**. Candidate speed versus CPython is **0.138×** (**7.26× longer runtime**). Nominal speedup versus preceding XLang3 is **1.003×**. Sample SD **0.203 s**; pyperf instability warning **False**. Reused references do not prove paired significance.

The original BPE source/workload, fast mode, shared hook/dependency site and
1,800-second cap remain. Calibration/warmups are unscored. BPE native key entry
was already optimized in the preceding checkpoint; the new ordinary-call path
does not imply another native callback gain. This single case neither replaces
the complete 97-case report nor proves a whole-suite CPython win. GC traversal
remains withheld from official comparisons.

Seven alternating diagnostic pairs preserve **770 samples and 11 rows**, including
neutral/slower controls. Each process checks outputs and warms each row. Ratios
above 1× mean faster than preceding XLang3, not faster than CPython. In particular,
dict_get_default is slower in every pair; this control remains visible even though
the unchanged fixed gate passed. Diagnostics include Python loops and comparisons;
they do not isolate the cost of one native operation.

| Probe | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
| trivial_callback | direct_constant  | 1.025× | 6/7 |
| trivial_callback | native_max_constant_key  | 0.991× | 3/7 |
| trivial_callback | counter_missing  | 0.995× | 3/7 |
| trivial_callback | direct_counter_missing  | 0.978× | 1/7 |
| trivial_callback | dict_get_default  | 0.955× | 0/7 |
| nontrivial_callback | direct_lookup dict | 1.052× | 6/7 |
| nontrivial_callback | python_call dict | 1.749× | 7/7 |
| nontrivial_callback | native_callback dict | 0.997× | 3/7 |
| nontrivial_callback | direct_lookup Counter | 0.992× | 3/7 |
| nontrivial_callback | python_call Counter | 1.736× | 7/7 |
| nontrivial_callback | native_callback Counter | 1.044× | 6/7 |

[Validation](data/vm-captured-lookup-validation-20261007.json),
[official comparison](data/vm-captured-lookup-bpe-vs-cpython3147-20261007.json),
[paired raw record](data/vm-captured-lookup-paired-20261007.json),
[samples](data/vm-captured-lookup-paired-samples-20261007.csv),
[summary](data/vm-captured-lookup-paired-summary-20261007.csv),
[fixed gate](data/release-vm-captured-lookup-fixed-gate-20261007.json),
[compiled source provenance](data/vm-captured-lookup-source-provenance-20261007.json),
[source archive](data/vm-captured-lookup-20261007-sources/manifest.json),
[last full comparison](pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007.md).
