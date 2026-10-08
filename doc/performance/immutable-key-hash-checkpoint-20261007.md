# Completed immutable-key hash caches

Exact immutable bytes now retain their native hash. Completed tuples cache hashes
only when every member is callback-free and immutable: None, bool, immediate int,
exact string/bytes/BigInt, or an eligible nested tuple. The native and runtime hash
paths share only this proven intrinsic result. User objects, classes, floats,
memoryviews and other unsupported members retain their existing hash path;
failures and Python callbacks are not cached. This is a narrower cache than
CPython 3.14.7's tuple cache for arbitrary Python objects, and does not claim to
close that existing conformance gap.

The gain comes from avoiding repeated component hashing in dict read/modify/write
and callback lookups. Hash values and the existing mixing algorithm are unchanged.
Atomic cache slots permit concurrent immutable readers. The tuple freelist resets
old caches before reuse. Reserved tuples and marshal placeholders remain uncached
until complete; VM/IR/graph builders explicitly finish construction, and graph
edge clearing invalidates the cache. Native bytes construction invalidates its
cache before requesting the mutable construction pointer. Published bytes and
tuples remain immutable. Code comments document these guards and lifecycle rules.

Counter, BPE and all pure-Python libraries remain Python. This generic runtime
optimization does not translate them into C++ or reuse CPython native modules.

## Validation

The candidate passed **373 core fixtures**, 11 compatibility sections, three
expected-failure checks, eight C++/SDK/graph checks, and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
New C++ checks cover actual cache eligibility, nested hashes, freelist reuse,
partial builders, invalidation/failures, native-versus-runtime identity fallback,
and simultaneous read-only hashing. The Python fixture checks stable hashes,
numeric-key equality, unhashable members, recycling, frozen bytes, marshal/pickle
reconstruction, Python callbacks and tracing.

The accepted control preserves 140 Release files. Build/run paths remain
build-repro/main-verify-20261006/Release, and comparison Python is 3.14.7.

Official means: CPython 3.14.7 **3.586 s**, preceding XLang3 **31.699 s**, candidate **27.914 s**. Candidate speed relative to CPython is **0.128×** (**7.78× longer runtime**); nominal speedup over preceding XLang3 is **1.136×**. Candidate sample SD is **0.186 s**. Pyperf instability warning: **False**. These reused references do not establish an alternating-pair significance claim.

Official BPE keeps the original source, vocabulary, input data, fast mode, shared
hook/dependency site and 1,800-second cap. Warmups/calibration are excluded from
scoring. This affected case does not replace the complete 97-case comparison or
establish a whole-suite win against CPython. The official GC traversal score is
still withheld; its fixed-gate row is only a baseline regression check.

## Paired diagnostics

Seven alternating control/candidate process pairs retain all **1,834 samples**
and **31 rows**. Hash/callback probes use five samples and dictionary update
scaling uses three; every row has a warmup and checks its outputs. Ratios above
1× mean faster than preceding XLang3, not CPython. The separate 80-sample baseline
includes Python loop/assertion overhead and is not an isolated native hash score.

All slower controls remain visible, including direct constant calls and a fresh
tuple hashed only once. A reused hash can pay back its caching cost; a fresh key
with one hash need not improve. No leaf ratio is presented as a whole-suite gain.

| Probe | Case | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
| hash | bytes 1 B, fresh=False, hashes=1 | 1.000× | 4/7 |
| hash | bytes 32 B, fresh=False, hashes=1 | 0.991× | 2/7 |
| hash | bytes 256 B, fresh=False, hashes=1 | 1.125× | 7/7 |
| hash | tuple_bytes 1 B, fresh=False, hashes=1 | 1.509× | 7/7 |
| hash | tuple_bytes 32 B, fresh=False, hashes=1 | 1.528× | 7/7 |
| hash | tuple_bytes 256 B, fresh=False, hashes=1 | 1.688× | 7/7 |
| hash | tuple_bytes 32 B, fresh=True, hashes=1 | 0.980× | 1/7 |
| hash | tuple_bytes 32 B, fresh=True, hashes=2 | 1.238× | 7/7 |
| update | bytes dict, keys=64, fresh=False | 1.007× | 6/7 |
| update | bytes dict, keys=256, fresh=False | 1.001× | 4/7 |
| update | bytes dict, keys=1024, fresh=False | 1.016× | 5/7 |
| update | tuple_bytes dict, keys=64, fresh=False | 1.944× | 7/7 |
| update | tuple_bytes dict, keys=256, fresh=False | 1.922× | 7/7 |
| update | tuple_bytes dict, keys=1024, fresh=False | 1.912× | 7/7 |
| update | tuple_bytes dict, keys=64, fresh=True | 1.219× | 6/7 |
| update | tuple_bytes dict, keys=256, fresh=True | 1.232× | 7/7 |
| update | tuple_bytes dict, keys=1024, fresh=True | 1.226× | 7/7 |
| update | tuple_bytes Counter, keys=64, fresh=True | 1.214× | 7/7 |
| update | tuple_bytes Counter, keys=256, fresh=True | 1.198× | 7/7 |
| update | tuple_bytes Counter, keys=1024, fresh=True | 1.222× | 7/7 |
| trivial_callback | direct_constant | 0.967× | 1/7 |
| trivial_callback | native_max_constant_key | 0.989× | 2/7 |
| trivial_callback | counter_missing | 1.947× | 7/7 |
| trivial_callback | direct_counter_missing | 0.989× | 1/7 |
| trivial_callback | dict_get_default | 2.048× | 7/7 |
| nontrivial_callback | dict direct_lookup | 2.002× | 7/7 |
| nontrivial_callback | dict python_call | 1.596× | 7/7 |
| nontrivial_callback | dict native_callback | 1.343× | 7/7 |
| nontrivial_callback | Counter direct_lookup | 1.894× | 7/7 |
| nontrivial_callback | Counter python_call | 1.526× | 7/7 |
| nontrivial_callback | Counter native_callback | 1.316× | 7/7 |

## Evidence

- [Terminal validation](data/immutable-key-hash-validation-20261007.json)
- [Fixed gate](data/release-immutable-key-hash-fixed-gate-20261007.json)
- [Original official log](data/immutable-key-hash-validation-20261007-official-bpe.log)
- [Official comparison](data/immutable-key-hash-bpe-vs-cpython3147-20261007.json)
- [Initial CPython/XLang3 observations](data/immutable-key-hash-baseline-20261007.json)
- [Original paired results](data/immutable-key-hash-paired-20261007.json)
- [All samples](data/immutable-key-hash-paired-samples-20261007.csv)
- [All summary rows](data/immutable-key-hash-paired-summary-20261007.csv)
- [Compiler source provenance](data/immutable-key-hash-source-provenance-20261007.json)
- [Archived scripts and compiler inputs](data/immutable-key-hash-20261007-sources/manifest.json)
- [Preserved control](data/immutable-key-hash-preserved-control-20261007.json)
- [Preceding checkpoint](dict-missing-special-lookup-checkpoint-20261007.md)

Reference implementation: [CPython 3.14.7 tupleobject.c](https://github.com/python/cpython/blob/v3.14.7/Objects/tupleobject.c), tuple_hash and tuple_alloc.
