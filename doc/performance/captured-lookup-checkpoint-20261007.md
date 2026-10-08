# Native entry for captured dictionary lookup functions

The original BPE key callback compiles to five IR instructions: LoadFree stats,
LoadLocal x, GetItem, Return, and implicit ReturnConst None. Its names already
bind by index. Native callbacks now recognize that generic current IR shape and
resolve the live captured cell directly, avoiding repeated Interpreter/frame
creation only for a proven nonfallible dictionary hit. The original Python
function and Counter library remain authoritative; no library is translated
into C++ and no CPython native module is reused.

The lookup requires an already valid intrinsic hash index, a callback-free query
and key set, and exact native dict storage semantics. Dict subclasses also
require their currently resolved __getitem__ to have the registered canonical
callback and fast adapter, ordinary descriptor binding and no contextual user
data. Display names alone cannot authorize this path. Invalid indices, misses,
overrides, custom hash/equality, descriptors and fallible cases use the original
Python call without speculative user-code execution or replay.

The analyzer checks current code/signature on entry, so code replacement is
observed without a persistent body cache. Generators, async/coroutine functions,
unsupported signatures and own cell locals are excluded. Debugging, tracing,
profiling, function monitoring and pending asynchronous work retain normal
entry. Receiver/key remain borrowed during the callback-free query; the result
is owned before old-output release, and no dictionary/key storage is touched
after a finalizer can run. Comments explain the cost avoided and these guards.

## Validation and retained failure

The corrected candidate passed **374 core fixtures**, 11 compatibility sections,
three expected-failure checks, eight C++/SDK/graph checks and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
The seven-part fixture uses tuple-key hits to exercise eligible general-index
lookups, and covers live cells, overrides/misses, key protocols/descriptors,
errors/tracebacks, code replacement, handled exceptions, trace/profile and
monitoring. C++ checks cover eligibility, unchanged miss output, invalid indices,
unproven query types, canonical getter identity, finalizer mutation and receiver/
output aliasing.

The first full validation passed Python fixtures but failed two C++ assertions:
the lifetime tests used an integer-only dictionary, which uses its separate
scalar index and is intentionally ineligible for this shortcut. The corrected
tests use the tuple-key general index. The original source snapshot and failure
logs remain. An unrelated xMind build was observed and left alone; the mutation
guard delayed engine editing until that build was terminal.

The accepted control preserves 140 Release files. Build/run paths remain
build-repro/main-verify-20261006/Release and comparison Python is 3.14.7.

Official means: CPython 3.14.7 **3.586 s**, preceding XLang3 **27.914 s**, candidate **26.090 s**. Candidate speed relative to CPython is **0.137×** (**7.28× longer runtime**); nominal speedup over preceding XLang3 is **1.070×**. Candidate sample SD is **0.386 s**. Pyperf instability warning: **True**. These reused references do not establish an alternating-pair significance claim.

Official BPE retains the original source, vocabulary, input, fast mode, shared
hook/dependency site and 1,800-second cap. Warmups/calibration are excluded from
scoring. The separate pre-change IR diagnostic disabled only the __main__ runner
and preserved the trainer source bytes; it was unscored. This case does not
replace the complete 97-case comparison or establish a whole-suite CPython win.
The official GC traversal score remains withheld.

## Paired diagnostics

Seven alternating control/candidate process pairs retain all **1,834 samples**
and **31 rows**. Hash/callback probes use five samples; update scaling uses three.
Every row has a warmup and checked outputs. Ratios above 1× mean faster than the
preceding XLang3, not CPython. All neutral and slower controls remain. Native-vs-
direct loop differences include comparisons/ownership and are not isolated
frame-creation measurements.

| Probe | Case | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
| hash | bytes 1 B, fresh=False, hashes=1 | 1.008× | 4/7 |
| hash | bytes 32 B, fresh=False, hashes=1 | 1.001× | 4/7 |
| hash | bytes 256 B, fresh=False, hashes=1 | 1.012× | 6/7 |
| hash | tuple_bytes 1 B, fresh=False, hashes=1 | 0.975× | 2/7 |
| hash | tuple_bytes 32 B, fresh=False, hashes=1 | 0.993× | 3/7 |
| hash | tuple_bytes 256 B, fresh=False, hashes=1 | 0.969× | 1/7 |
| hash | tuple_bytes 32 B, fresh=True, hashes=1 | 1.003× | 4/7 |
| hash | tuple_bytes 32 B, fresh=True, hashes=2 | 1.013× | 4/7 |
| update | bytes dict, keys=64, fresh=False | 1.046× | 7/7 |
| update | bytes dict, keys=256, fresh=False | 1.047× | 7/7 |
| update | bytes dict, keys=1024, fresh=False | 1.040× | 7/7 |
| update | tuple_bytes dict, keys=64, fresh=False | 1.026× | 6/7 |
| update | tuple_bytes dict, keys=256, fresh=False | 1.041× | 6/7 |
| update | tuple_bytes dict, keys=1024, fresh=False | 1.035× | 7/7 |
| update | tuple_bytes dict, keys=64, fresh=True | 1.020× | 6/7 |
| update | tuple_bytes dict, keys=256, fresh=True | 1.022× | 7/7 |
| update | tuple_bytes dict, keys=1024, fresh=True | 1.020× | 7/7 |
| update | tuple_bytes Counter, keys=64, fresh=True | 1.012× | 5/7 |
| update | tuple_bytes Counter, keys=256, fresh=True | 1.016× | 6/7 |
| update | tuple_bytes Counter, keys=1024, fresh=True | 1.011× | 5/7 |
| trivial_callback | direct_constant | 0.966× | 0/7 |
| trivial_callback | native_max_constant_key | 1.042× | 6/7 |
| trivial_callback | counter_missing | 0.992× | 3/7 |
| trivial_callback | direct_counter_missing | 1.013× | 6/7 |
| trivial_callback | dict_get_default | 1.035× | 5/7 |
| nontrivial_callback | dict direct_lookup | 1.010× | 5/7 |
| nontrivial_callback | dict python_call | 0.996× | 3/7 |
| nontrivial_callback | dict native_callback | 2.807× | 7/7 |
| nontrivial_callback | Counter direct_lookup | 1.041× | 7/7 |
| nontrivial_callback | Counter python_call | 1.015× | 4/7 |
| nontrivial_callback | Counter native_callback | 2.741× | 7/7 |

## Evidence

- [Corrected terminal validation](data/captured-lookup-validation-r2-20261007.json)
- [Fixed gate](data/release-captured-lookup-r2-fixed-gate-20261007.json)
- [Original official log](data/captured-lookup-validation-r2-20261007-official-bpe.log)
- [Official comparison](data/captured-lookup-bpe-vs-cpython3147-20261007.json)
- [First failed validation](data/captured-lookup-validation-20261007.json)
- [Original paired results](data/captured-lookup-paired-20261007.json)
- [All samples](data/captured-lookup-paired-samples-20261007.csv)
- [All summary rows](data/captured-lookup-paired-summary-20261007.csv)
- [Compiler source provenance](data/captured-lookup-source-provenance-20261007.json)
- [Archived scripts, original IR and source inputs](data/captured-lookup-20261007-sources/manifest.json)
- [Preserved control](data/captured-lookup-preserved-control-20261007.json)
- [Preceding checkpoint](immutable-key-hash-checkpoint-20261007.md)
