# Intrinsic dictionary index trial versus CPython 3.14.7

This checkpoint removes a linear scan from tuple/bytes dictionary writes and
adds a guarded read path. It materially improves a Counter diagnostic, but
XLang3 remains slower than CPython 3.14.7. The final official BPE run timed out
at 1,200 seconds; this document does not claim a completed pyperformance win.

## Implementation and compatibility

`src/runtime/mapping.cpp` shares the existing runtime hash buckets for exact
intrinsic keys. The query and the entire stored key set must have callback-free
hashing and equality. Eligible keys include bytes, strings, native integer
values, None, and recursively eligible tuples. Float, buffer, subclass and
custom protocol keys retain the existing fallback. Rejected eligibility is
cached so mixed dictionaries do not rescan all keys before every write.

The ordered entry vector remains authoritative. Hash buckets store scalar entry
locations and do not retain extra key/value owners. Delete, clear, popitem and
object recycling invalidate or reset the index metadata. Reads keep subclass
`__getitem__` overrides and `__missing__` dispatch. The guarded read avoids a
second hash and Python equality dispatch; it owns the result before replacing
an output value whose finalizer might mutate the dictionary.

The replacement probe exposed an existing access violation in both the trial
and the preserved earlier Release. The new write path retains the incoming
item, detaches the old value, publishes the replacement, and only then releases
the old owner. Its finalizer can clear/repopulate the dictionary without leaving
an assignment into invalidated storage. Scalar updates keep the direct path.
This repair applies to the new intrinsic write path; it is not a claim that all
other container replacement paths have been audited or repaired.

Python `collections.Counter` and the official BPE algorithm remain Python.
There is no native translation of their implementation. Code comments record
the complexity improvement, guard requirements, index ownership and finalizer
ordering. The fixture covers ordering, equal distinct keys, collisions, nested
large integers, mixed/custom keys, subclass overrides, missing keys, deletion,
clear and finalizer reentry. C++ coverage checks index eligibility, invalidation,
capacity retention and absence of extra key owners.

## Diagnostic timings

The Counter scaling probe performs 4,096 read/modify/write updates to 1,024
tuple-of-bytes keys. It creates a fresh tuple for each update and verifies all
values, key order and checksums. Three samples follow one warmup. These are
diagnostic medians, not pyperf scores or statistical significance claims.

| Runtime / trial | Median time | Relative to earlier XLang3 |
| --- | ---: | ---: |
| CPython 3.14.7 | 0.519 ms | Reference |
| Preserved earlier XLang3 | 503.624 ms | 1.00× |
| Indexed writes | 14.302 ms | 35.21× faster |
| Indexed writes and guarded reads | 13.287 ms | 37.90× faster |
| Finalizer-safe indexed writes and guarded reads | 13.721 ms | 36.71× faster |

The final diagnostic is still about **26.5× slower than CPython**. Variation
between the two read-index trials means the small incremental read gain should
not be presented as a precise or established percentage improvement.

One fresh-process, unchanged complete official BPE workload took 3.623 seconds
on CPython and 44.129 seconds on the read-index trial, about **12.18× slower**.
Its original round-trip assertion passed. The preserved earlier XLang3 process
did not finish within 180 seconds, including setup; no body time or numerical
speedup is assigned to that timeout. This observation is not a full
pyperformance result, and it precedes the finalizer repair.

A separate instrumented execution of the full algorithm attributed work to
phases. Both runtimes produced the same 1,024 vocabulary entries and 10,335
encoded tokens. Instrumentation changes timing, so the phase totals must not
replace the unchanged-body timing or an official score.

| Phase | CPython 3.14.7 | Read-index trial XLang3 |
| --- | ---: | ---: |
| Setup | 0.004 s | 0.058 s |
| Counting pairs | 2.104 s | 25.689 s |
| Selecting the next merge | 0.101 s | 4.303 s |
| Applying merges | 1.326 s | 9.037 s |
| Encoding | 0.033 s | 0.280 s |

Counting accounts for about 65% of the instrumented XLang3 total. The next
diagnostic separates direct dictionary reads, Python calls in a loop, and
callbacks made by the native `max` builtin. It completed after the official run
became terminal. In the checked dictionary workload, the candidate's medians
were 7.835 ms for direct lookup, 10.857 ms for a Python lookup call in a loop,
and 15.525 ms for `max` with the same callback (8,192 lookups per sample).
Different loop shapes make this a diagnosis, not a before/after optimization
claim.

A separate subscript probe performed 4,096 updates to reused tuple keys. On the
candidate, a class directly exposing native dict methods took 7.954 ms versus
9.848 ms for its subclass inheriting those same methods. Direct Python methods
took 10.981 ms versus 18.185 ms for an otherwise equivalent inheriting subclass.
CPython's corresponding direct/inherited timings were essentially alike:
0.187/0.189 ms for native methods and 0.433/0.433 ms for Python methods. These
are five-sample diagnostic medians following a warmup, with all values checked.
The VM's current method cache resolves only attributes directly stored on the
class, which gives a concrete next target: reuse its guarded call path for
methods found through the MRO. No inherited-method optimization is included in
this checkpoint.

The `min`/`max` semantic probe also confirmed that XLang3 consumes all items
before calling keys and comparisons, whereas CPython interleaves them and stops
on the first failure. A key callback appending 3 to the input list [1, 2] made
CPython return 3 and XLang3 return 2. Repairing the native streaming protocol is
outstanding; this dictionary checkpoint does not fix it.

Output fingerprints match CPython:

- Vocabulary SHA-256: `886a868d65c2ad4adc851b073091cc364cf39e20691406a6015631468af06fff`
- Encoded token SHA-256: `a4cce189a198bca69c43894cc135b07acf7936f9c883a07023325cbb34bc0e0d`

## Validation and remaining work

The final candidate passed 367 core fixtures, 11 compatibility sections,
3 expected failures and all 8 selected C++/SDK/graph tests. The unchanged fixed
Release gate passed all 11 cases using its default 21 pairs, 5 warmups and 10%
per-case tolerance. The fixed baseline was not updated.

Official BPE trials for indexed writes and guarded reads both timed out at the
original 300-second full-case cap. Those failures remain preserved and unscored.
The finalizer-safe candidate ran the unchanged official fast workload with a
1,200-second observation cap and also timed out. Its manager produced eight
progress dots but no completed benchmark JSON. There is no valid official BPE
score or full-case win to assign. The observation limit did not shorten the
workload or change its samples; the prior full-suite dataset remains intact.

The latest completed full-suite report remains the separate 97-case comparison
after live-eval fixes: 72 completed XLang3 definitions, 25 failures and about
8.11× slower overall on 80 scored subtests. No full-suite refresh of this index
candidate has completed. GC coverage, class-body lowering, native module gaps
and other failures remain outstanding.

## Evidence

- [Earlier XLang3 and CPython scaling samples](data/dict-composite-scaling-baseline-20261007.json)
- [All 60 diagnostic medians and relative speeds](data/dict-intrinsic-index-20261007-diagnostic-medians.csv)
- [All 180 checked diagnostic samples](data/dict-intrinsic-index-20261007-diagnostic-samples.csv)
- [Diagnostic CSV export hashes](data/dict-intrinsic-index-20261007-diagnostic-export.json)
- [Exact diagnostic/controller source manifest](data/dict-intrinsic-index-20261007-sources/manifest.json)
- [Final validation and raw phase logs](data/dict-native-index-final-validation-20261007.json)
- [Fixed default regression gate](data/release-dict-native-index-final-fixed-gate-20261007.json)
- [Guarded-read trial, including official timeout](data/dict-native-read-index-validation-20261007.json)
- [Write-index correctness, scaling and gate](data/dict-native-key-index-validation-r2-20261007.json)
- [Write-index official retry timeout](data/pyperformance-dict-native-key-index-bpe-fast-r3-20261007-provenance.json)
- [Unchanged full official body observations](data/bpe-official-body-dict-index-observations-20261007.json)
- [Instrumented phase observations and complete output checks](data/bpe-dict-index-phase-observations-20261007.json)
- [Finalizer access violation observations before repair](data/dict-intrinsic-overwrite-reentry-before-20261007.json)
- [Six completed dispatch/callback/streaming observations](data/dict-native-dispatch-followup-20261007.json)
- [Previous full-suite report](pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007.md)

Candidate run/build location remains
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
Release controls are preserved separately; the executable run path is unchanged.
Only `C:\Python\Python314\python.exe` (3.14.7) is the reference interpreter.
